// VKey - Key Event Sink Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "stdafx.h"
#include "KeyEventSink.h"
#include "TextService.h"
#include "EngineController.h"
#include "CompositionEditSession.h"
#include "ComUtils.h"
#include "Define.h"

namespace NextKey {
namespace TSF {

// Convert VK code + lParam to the Unicode character the active layout would produce.
// Uses ToUnicode so it respects US QWERTY, shift state, etc. Returns 0 if not printable.
static wchar_t VkToChar(UINT vk, LPARAM lParam) {
    BYTE keyState[256] = {};
    if (!GetKeyboardState(keyState)) return 0;
    UINT scanCode = (static_cast<UINT>(lParam) >> 16) & 0xFF;
    wchar_t buf[4] = {};
    int result = ToUnicode(vk, scanCode, keyState, buf, 4, 0);
    return (result == 1 && buf[0] != 0) ? buf[0] : 0;
}

// Helper function to check if a key is punctuation/number that should trigger commit
static bool IsPunctuationKey(UINT vkCode) {
    // Number keys (0-9)
    if (vkCode >= 0x30 && vkCode <= 0x39) return true;

    // Numpad keys
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_DIVIDE) return true;

    // OEM keys (punctuation on US keyboard)
    // VK_OEM_1 (;:), VK_OEM_PLUS (=+), VK_OEM_COMMA (,<), VK_OEM_MINUS (-_)
    // VK_OEM_PERIOD (.>), VK_OEM_2 (/?), VK_OEM_3 (`~), VK_OEM_4 ([{)
    // VK_OEM_5 (\|), VK_OEM_6 (]}), VK_OEM_7 ('")
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_3) return true;
    if (vkCode >= VK_OEM_4 && vkCode <= VK_OEM_8) return true;
    if (vkCode == VK_OEM_PLUS || vkCode == VK_OEM_COMMA ||
        vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PERIOD) return true;

    // Tab key
    if (vkCode == VK_TAB) return true;

    return false;
}

KeyEventSink::KeyEventSink(TextService* pTextService, EngineController* pEngineController)
    : pTextService_(pTextService), pEngineController_(pEngineController) {
}

KeyEventSink::~KeyEventSink() {
    Unadvise();
}

bool KeyEventSink::Advise(ITfThreadMgr* pThreadMgr) {
    if (pThreadMgr == nullptr) return false;

    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr_);
    if (FAILED(hr)) return false;

    hr = pKeystrokeMgr_->AdviseKeyEventSink(
        pTextService_->GetClientId(),
        static_cast<ITfKeyEventSink*>(this),
        TRUE  // Foreground
    );

    if (FAILED(hr)) {
        SafeRelease(pKeystrokeMgr_);
        return false;
    }

    TSF_LOG(L"KeyEventSink advised");
    return true;
}

void KeyEventSink::Unadvise() {
    if (pKeystrokeMgr_) {
        pKeystrokeMgr_->UnadviseKeyEventSink(pTextService_->GetClientId());
        SafeRelease(pKeystrokeMgr_);
    }
    TSF_LOG(L"KeyEventSink unadvised");
}

IFACEMETHODIMP KeyEventSink::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) KeyEventSink::AddRef() {
    return InterlockedIncrement(&refCount_);
}

IFACEMETHODIMP_(ULONG) KeyEventSink::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    // Note: Do NOT delete this here. Lifetime is managed by unique_ptr in TextService.
    // COM ref counting is maintained for contract compliance only.
    return count;
}

IFACEMETHODIMP KeyEventSink::OnSetFocus(BOOL fForeground) {
    if (fForeground) {
        TSF_LOG(L"OnSetFocus: foreground");
        // Re-read SharedState on focus to pick up ENGINE_ENABLED/VIETNAMESE_MODE changes
        if (pEngineController_) {
            pEngineController_->CheckConfigEvent();
            pEngineController_->RefreshFlags();
            // Publish TIP active state — EXE reads this for tray icon sync
            pEngineController_->SetTsfTipActive(true);
            // Focus change invalidates the commit-undo window — cursor may have
            // moved arbitrarily relative to the cached lastCommit_ text.
            pEngineController_->ResetCommitUndo();
        }
    } else {
        TSF_LOG(L"OnSetFocus: background");
        if (pEngineController_) {
            pEngineController_->SetTsfTipActive(false);
            pEngineController_->ResetCommitUndo();
        }
    }
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;

    pEngineController_->CheckConfigEvent();

    // Drop any punct char cached by a previous OnTestKeyDown whose OnKeyDown pair
    // never fired (rare TSF anomaly).
    lastPunctChar_ = 0;

    // Check if this context blocks input (password, PIN, email fields)
    pEngineController_->CheckContextBlocked(pContext);
    if (pEngineController_->IsContextBlocked()) {
        *pfEaten = FALSE;
        return S_OK;
    }

    // Intercept VK_BACK for autocomplete suggestion dismissal
    if (wParam == VK_BACK && pEngineController_->HasEngineBuffer() &&
        pEngineController_->IsSuggestKeepCharsEnabled() &&
        pEngineController_->HasNonEmptySelection(pContext)) {
        TSF_LOG(L"OnTestKeyDown: backspace autocomplete suggestion detected -> commit and pass through");
        pEngineController_->Commit(pContext);
        *pfEaten = FALSE;
        lastTestedVk_ = static_cast<UINT>(wParam);
        lastWantKeyResult_ = false;
        return S_OK;
    }

    // Commit-undo invalidation (design 2026-05-17): any key besides BS/ESC during
    // Ready/Primed means the user has moved on — drop the cache to prevent stale
    // restore on a later ESC. Modifier keys (Ctrl/Alt/Win/Shift) also reset; if
    // the user is starting a chord, the undo window is over.
    if (wParam != VK_BACK && wParam != VK_ESCAPE) {
        pEngineController_->OnNonRestoreKey();
    }

    // Safety check: recover from engine/composition desync.
    //
    // State A: engine has buffer but TSF composition was externally ended.
    //          TSF side is already gone — Reset() just drops our orphan
    //          composition pointer. No doc mutation.
    // State B: TSF composition active but our engine is empty. TSF host still
    //          has visible pending composition text. We must properly end the
    //          TSF composition (SetCompositionText + EndComposition via edit
    //          session) or the stale composition lingers until the host ends
    //          it. This does mutate the doc in the test phase — accepted
    //          tradeoff because TerminateComposition-only leaks the composition.
    bool isComposing = pEngineController_->IsComposing();
    bool hasBuffer = pEngineController_->HasEngineBuffer();
    if (!isComposing && hasBuffer) {
        TSF_LOG(L"OnTestKeyDown: desync A (composition gone, buffer=%d) → Reset",
                static_cast<int>(pEngineController_->HasEngineBuffer()));
        pEngineController_->Reset();
    } else if (isComposing && !hasBuffer) {
        TSF_LOG(L"OnTestKeyDown: desync B (composition live, buffer empty) → Commit");
        pEngineController_->Commit(pContext);
    }

    // Check modifiers
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    // Modifiers -> commit and pass through
    if (ctrl || alt || win) {
        if (pEngineController_->HasEngineBuffer()) {
            pEngineController_->Commit(pContext);
        }
        *pfEaten = FALSE;
        return S_OK;
    }

    // Esc-restore-raw: end composition with the user's raw keys (víu → virus)
    // instead of the Vietnamese form. Committing in test phase is the right
    // pattern for action keys (per CLAUDE.md "TSF: commit-in-test-phase allowed
    // for action keys") — no text-insert race. The same branch in OnKeyDown
    // handles Chromium hosts that skip OnTestKeyDown.
    //
    // Post-BS extension (design 2026-05-17): when engine is empty but commit-undo
    // is Primed (user typed space then BS), restore raw from lastCommit_ cache
    // via TryRestoreLastCommitRaw.
    if (wParam == VK_ESCAPE && pEngineController_->IsEscRestoreRawEnabled()) {
        if (pEngineController_->HasEngineBuffer()) {
            if (pEngineController_->CommitRawAndEnd(pContext)) {
                *pfEaten = TRUE;
                return S_OK;
            }
        } else if (pEngineController_->IsCommitUndoPrimed()
                   && pEngineController_->WithinUndoWindow()) {
            if (pEngineController_->TryRestoreLastCommitRaw(pContext)) {
                *pfEaten = TRUE;
                return S_OK;
            }
        }
    }

    // (VK_RETURN falls through to the generic non-handled-key branch below:
    // WantKey returns false for Enter, so with a live buffer the branch commits
    // the composition and passes Enter to the host — search submits, newline
    // inserts, form submits, all on a single press.)

    // Punctuation + active composition → eat the key; OnKeyDown will commit with
    // this char appended (atomic "abc," — no race between EndComposition and the
    // host's default key handling).
    //
    // We only eat when VkToChar succeeds (can reproduce the printable char). For
    // the rare case where it fails (dead key, non-printable punct mapping), fall
    // through without eating — TSF spec forbids mutating the document in the test
    // phase, so no commit here. Composition will resolve via normal flow in
    // OnKeyDown, which may not fire for an un-eaten key; worst case the user sees
    // a momentarily stale composition with an uneaten punct arriving at caret.
    // Acceptable for an edge case essentially never hit on US QWERTY.
    bool isPunctuation = IsPunctuationKey(static_cast<UINT>(wParam));
    if (isPunctuation && pEngineController_->HasEngineBuffer()) {
        bool isEngineDigit = pEngineController_->IsEngineDigitKey(static_cast<UINT>(wParam));
        if (!isEngineDigit) {
            wchar_t ch = VkToChar(static_cast<UINT>(wParam), lParam);
            if (ch != 0) {
                *pfEaten = TRUE;
                lastTestedVk_ = static_cast<UINT>(wParam);
                lastWantKeyResult_ = true;
                lastPunctChar_ = ch;  // OnKeyDown reads this — no second ToUnicode call.
                return S_OK;
            }
            TSF_LOG(L"OnTestKeyDown: VkToChar failed vk=0x%02X, passthrough (no eat)",
                    (UINT)wParam);
            // Fall through to WantKey / normal flow — no doc mutation in test phase.
        }
    }

    bool wantKey = pEngineController_->WantKey(static_cast<UINT>(wParam), true);

    if (wParam == VK_BACK) {
        bool suggestKeep = pEngineController_->IsSuggestKeepCharsEnabled();
        bool notEmpty = false;
        if (pContext != nullptr && suggestKeep) {
            auto* pSession = new SelectionCheckEditSession(pContext, &notEmpty);
            HRESULT hrSession = S_OK;
            HRESULT hr = pContext->RequestEditSession(
                pTextService_->GetClientId(), pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
            pSession->Release();
            if (SUCCEEDED(hr) && SUCCEEDED(hrSession) && notEmpty) {
                wantKey = false;
            }
        }
        TSF_LOG(L"OnTestKeyDown: VK_BACK wantKey=%d suggestKeep=%d notEmptySelection=%d",
                wantKey, suggestKeep, notEmpty);
    }

    // Commit-undo BS detection (design 2026-05-17): Ready → Primed.
    // When the user just CommitWithChar'd a word (space appended) and then
    // presses BS, they're undoing the just-committed word — not asking to
    // revive it back into composition. Transition state and let BS pass
    // through to the host (host deletes the space). ESC arriving next will
    // restore raw via TryRestoreLastCommitRaw. Second BS in Primed → cancel.
    // Must run BEFORE BackspaceRevive so revive doesn't compete with undo.
    if (wParam == VK_BACK && !pEngineController_->HasEngineBuffer()) {
        if (pEngineController_->IsCommitUndoReady()
            && pEngineController_->WithinUndoWindow()) {
            pEngineController_->TransitionUndoReadyToPrimed();
            // Skip BackspaceRevive — user intent is undo, not revive.
            *pfEaten = FALSE;
            lastTestedVk_ = static_cast<UINT>(wParam);
            lastWantKeyResult_ = false;
            return S_OK;
        }
        if (pEngineController_->IsCommitUndoPrimed()) {
            // Second BS — user is now deleting committed body, drop the undo window.
            pEngineController_->ResetCommitUndo();
            // Fall through to BackspaceRevive (it may want to claim this BS).
        }
    }

    // Backspace revive: if engine is empty and cursor is right after a Vietnamese
    // word, claim the BS and re-enter composition in HandleKey. Pre-read the word
    // here (sync edit session) so we can decide whether to eat the key.
    if (!wantKey && wParam == VK_BACK && !pEngineController_->HasEngineBuffer()) {
        if (pEngineController_->PrepareBackspaceRevive(pContext)) {
            wantKey = true;
        }
    }

    // Cache result so OnKeyDown can reuse without calling WantKey again
    lastTestedVk_ = static_cast<UINT>(wParam);
    lastWantKeyResult_ = wantKey;

    // Non-handled key with active buffer → commit and pass through. Action and
    // navigation keys (arrows, Escape, F-keys, Home/End, Delete) take effect on
    // a single press. Printable keys that could race with commit text are
    // handled by earlier branches (punct above, A-Z/space via HandleKey), so
    // they don't reach here.
    if (!wantKey && pEngineController_->HasEngineBuffer()) {
        pEngineController_->Commit(pContext);
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = wantKey ? TRUE : FALSE;
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnTestKeyUp(ITfContext* /*pContext*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;
    // Eat keyup for A-Z and Backspace during active composition
    // (prevents apps from seeing keyup without corresponding keydown)
    // Do NOT call WantKey() here — it has side effects (auto-cap state machine)
    if (pEngineController_->IsComposing()) {
        UINT vk = static_cast<UINT>(wParam);
        *pfEaten = (vk >= 0x41 && vk <= 0x5A) || vk == VK_BACK ? TRUE : FALSE;
    } else {
        *pfEaten = FALSE;
    }
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;

    pEngineController_->CheckConfigEvent();

    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    if (ctrl || alt || win) {
        *pfEaten = FALSE;
        return S_OK;
    }

    UINT vk = static_cast<UINT>(wParam);

    // Intercept VK_BACK for autocomplete suggestion dismissal (Chromium fallback)
    if (vk == VK_BACK && pEngineController_->HasEngineBuffer() &&
        pEngineController_->IsSuggestKeepCharsEnabled() &&
        pEngineController_->HasNonEmptySelection(pContext)) {
        TSF_LOG(L"OnKeyDown: backspace autocomplete suggestion detected -> commit and pass through");
        pEngineController_->Commit(pContext);
        lastTestedVk_ = 0;
        lastPunctChar_ = 0;
        *pfEaten = FALSE;
        return S_OK;
    }

    // Esc-restore-raw: end composition with raw keys (víu → virus) when enabled
    // and buffer non-empty. Eat the key (don't pass to app). Falls through to
    // standard ESC handling (commit Vietnamese + pass-through) when disabled
    // or buffer empty.
    //
    // Post-BS extension (design 2026-05-17): same logic as OnTestKeyDown — when
    // engine is empty but commit-undo is Primed, restore from lastCommit_ cache.
    if (vk == VK_ESCAPE && pEngineController_->IsEscRestoreRawEnabled()) {
        if (pEngineController_->HasEngineBuffer()) {
            if (pEngineController_->CommitRawAndEnd(pContext)) {
                *pfEaten = TRUE;
                return S_OK;
            }
        } else if (pEngineController_->IsCommitUndoPrimed()
                   && pEngineController_->WithinUndoWindow()) {
            if (pEngineController_->TryRestoreLastCommitRaw(pContext)) {
                *pfEaten = TRUE;
                return S_OK;
            }
        }
    }

    // Punctuation: commit composition with this char appended (atomic, no race).
    // Prefer the char cached by OnTestKeyDown (avoids a second ToUnicode call that
    // could mutate kernel dead-key state on some layouts). Fall back to a direct
    // VkToChar when the cache is empty — some hosts (notably Chromium) skip
    // OnTestKeyDown entirely and route keystrokes straight to OnKeyDown, so the
    // cache never gets populated. Only one ToUnicode call per keystroke either way.
    if (IsPunctuationKey(vk) && pEngineController_->HasEngineBuffer()
        && !pEngineController_->IsEngineDigitKey(vk)) {
        wchar_t ch = (vk == lastTestedVk_ && lastPunctChar_ != 0)
                       ? lastPunctChar_
                       : VkToChar(vk, lParam);
        if (ch != 0) {
            pEngineController_->CommitWithChar(pContext, ch);
            lastTestedVk_ = 0;
            lastPunctChar_ = 0;
            *pfEaten = TRUE;
            return S_OK;
        }
        // VkToChar failed (dead key / non-printable punct mapping). Fall through
        // to normal flow — worst case the punct arrives at the caret after the
        // composition, which is what would happen without any IME anyway.
        TSF_LOG(L"OnKeyDown: VkToChar failed vk=0x%02X, falling through", vk);
    }

    bool wantKey = false;
    if (vk == lastTestedVk_) {
        wantKey = lastWantKeyResult_;
    } else {
        wantKey = pEngineController_->WantKey(vk, true);
        if (pContext != nullptr && wantKey && vk == VK_BACK && pEngineController_->IsSuggestKeepCharsEnabled()) {
            bool notEmpty = false;
            auto* pSession = new SelectionCheckEditSession(pContext, &notEmpty);
            HRESULT hrSession = S_OK;
            HRESULT hr = pContext->RequestEditSession(
                pTextService_->GetClientId(), pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
            pSession->Release();
            if (SUCCEEDED(hr) && SUCCEEDED(hrSession) && notEmpty) {
                wantKey = false;
            }
        }
    }
    if (vk == VK_BACK) {
        TSF_LOG(L"OnKeyDown: VK_BACK wantKey=%d lastTestedVk=%u lastWantKeyResult=%d",
                wantKey, lastTestedVk_, lastWantKeyResult_);
    }
    lastTestedVk_ = 0;  // Invalidate cache
    lastPunctChar_ = 0;

    // Non-handled key with active buffer → commit composition and pass the key
    // through so action keys (Enter submits, Escape cancels, F-keys, arrows,
    // Home/End) take effect on a single press. The "Chrome cursor race" only
    // affects printable keys racing with commit text; those are already handled
    // (punct via the branch above, A-Z/space via HandleKey), so nothing text-
    // inserting reaches this point. Works whether OnTestKeyDown fired or not.
    if (!wantKey && pEngineController_->HasEngineBuffer()) {
        pEngineController_->Commit(pContext);
        *pfEaten = FALSE;
        return S_OK;
    }

    if (!wantKey) {
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = pEngineController_->HandleKey(pContext, vk) ? TRUE : FALSE;
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnKeyUp(ITfContext* /*pContext*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;
    if (pEngineController_->IsComposing()) {
        UINT vk = static_cast<UINT>(wParam);
        *pfEaten = (vk >= 0x41 && vk <= 0x5A) || vk == VK_BACK ? TRUE : FALSE;
    } else {
        *pfEaten = FALSE;
    }
    return S_OK;
}

IFACEMETHODIMP KeyEventSink::OnPreservedKey(ITfContext* /*pContext*/, REFGUID /*rguid*/, BOOL* pfEaten) {
    if (pfEaten == nullptr) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

}  // namespace TSF
}  // namespace NextKey
