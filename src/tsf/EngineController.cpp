// NexusKey - Engine Controller Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "stdafx.h"
#include "EngineController.h"
#include "CompositionEditSession.h"
#include "InputScopeChecker.h"
#include "Define.h"
#include "core/engine/EngineFactory.h"
#include <memory>

namespace NextKey {
namespace TSF {

EngineController::EngineController() {
    // Try to open SharedState from main app (read-write for flag toggling)
    if (sharedState_.OpenReadWrite()) {
        // Step 1: ABI check — direct header read, immune to seqlock contention.
        // magic/structVersion/structSize never change after Create(), so this
        // answer is stable and cannot spuriously flip TSF_ABI_MISMATCH under
        // concurrent writer activity.
        if (!sharedState_.IsAbiCompatible()) {
            abiOk_ = false;
            sharedState_.SetOrClearFlag(SharedFlags::TSF_ABI_MISMATCH, true);
            config_.inputMethod = InputMethod::Telex;
            config_.spellCheckEnabled = false;
            config_.optimizeLevel = 0;
            currentMethod_ = InputMethod::Telex;
            engine_ = EngineFactory::Create(config_);
            TSF_LOG(L"EngineController: SharedState ABI mismatch — passthrough");
        } else {
            // Step 2: ABI OK; try a seqlock Read for the full config.
            SharedState state = sharedState_.Read();
            if (state.IsValid()) {
                ApplySharedState(state);
                lastEpoch_ = state.epoch;
                TSF_LOG(L"EngineController initialized from SharedState (epoch=%u, method=%d)",
                        state.epoch, state.inputMethod);
            } else {
                // Seqlock exhausted under contention — use defaults for now.
                // RefreshFlags / CheckConfigEvent will re-read on next focus.
                // Do NOT flip TSF_ABI_MISMATCH — ABI is fine.
                config_.inputMethod = InputMethod::Telex;
                config_.spellCheckEnabled = false;
                config_.optimizeLevel = 0;
                currentMethod_ = InputMethod::Telex;
                engine_ = EngineFactory::Create(config_);
                TSF_LOG(L"EngineController: SharedState read contention, using defaults");
            }
        }
    } else {
        // SharedState not available = EXE not running → disabled
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = false;
        config_.optimizeLevel = 0;
        currentMethod_ = InputMethod::Telex;
        engine_ = EngineFactory::Create(config_);
        engineEnabled_ = false;
        TSF_LOG(L"EngineController: SharedState not available, engine disabled");
    }

    compositionMgr_.SetEngineController(this);
}

EngineController::~EngineController() {
    if (lastContext_) {
        lastContext_->Release();
        lastContext_ = nullptr;
    }
    ClearPendingRevive();
    TSF_LOG(L"EngineController destroyed");
}

void EngineController::ClearPendingRevive() {
    pendingReviveRange_.Release();
    pendingReviveWord_.clear();
}

bool EngineController::PrepareBackspaceRevive(ITfContext* pContext) {
    ClearPendingRevive();
    if (pContext == nullptr) return false;

    // Gate: only when engine is fully enabled AND Vietnamese mode AND engine empty.
    if (!engineEnabled_ || !tsfActive_ || !vietnameseMode_) return false;
    if (contextBlocked_) return false;
    if (engine_->Count() > 0) return false;
    // Scintilla (Notepad++) doesn't support ITfRange backward scan — skip to avoid
    // a guaranteed-to-fail sync edit session per BS.
    if (isScintillaApp_) return false;

    // Step 1: READ preceding word via sync edit session.
    auto* pSession = new ReadPrecedingWordEditSession(pContext);
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->RequestEditSession(
        clientId_, pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);

    std::wstring word;
    CComPtr<ITfRange> pRange;
    if (SUCCEEDED(hr) && SUCCEEDED(hrSession) && pSession->Found()) {
        word = pSession->Word();
        pRange.Attach(pSession->DetachRange());  // ownership transfer, no extra AddRef
    }
    pSession->Release();

    if (!pRange || word.empty()) return false;

    // Step 2: English-word gate via a throwaway engine (don't mutate engine_ —
    // safety resets at the top of each OnTestKeyDown would wipe it).
    auto tempEngine = EngineFactory::Create(config_);
    if (!tempEngine || !tempEngine->SeedFromText(word) || tempEngine->IsEnglishWord()) {
        TSF_LOG(L"PrepareBackspaceRevive: '%ls' rejected (not Vietnamese)", word.c_str());
        return false;  // pRange auto-Released
    }

    // Step 3: Cache {word, range} for HandleKey(VK_BACK). Engine_ stays empty.
    pendingReviveWord_ = std::move(word);
    pendingReviveRange_ = pRange;  // CComPtr = CComPtr → AddRefs (local copy stays valid)
    TSF_LOG(L"PrepareBackspaceRevive: armed for '%ls'", pendingReviveWord_.c_str());
    return true;
}

void EngineController::CheckContextBlocked(ITfContext* pContext) {
    if (pContext == lastContext_) return;  // Same context, use cached result

    // Release old context, AddRef new one (safe identity comparison)
    if (lastContext_) lastContext_->Release();
    lastContext_ = pContext;
    if (lastContext_) lastContext_->AddRef();
    contextBlocked_ = false;

    if (!pContext) return;

    auto* pSession = new InputScopeCheckSession(pContext, &contextBlocked_);
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->RequestEditSession(
        clientId_, pSession, TF_ES_SYNC | TF_ES_READ, &hrSession);
    pSession->Release();

    if (FAILED(hr) || FAILED(hrSession)) {
        // If we can't check, assume not blocked
        contextBlocked_ = false;
    }

    if (contextBlocked_) {
        TSF_LOG(L"Context blocked (password/PIN/email field)");
    }
}

bool EngineController::WantKey(UINT vkCode, bool /*isKeyDown*/) {
    // 0. ABI-mismatch safety gate: if this DLL's SharedState layout doesn't
    //    match what the main EXE is writing, pass every key through. Host
    //    process sees raw English typing until it restarts or the machine
    //    reboots — Settings dialog + tray render a banner via TSF_ABI_MISMATCH.
    if (!abiOk_) return false;

    // 1. Check if engine should process keys
    if (sharedState_.IsConnected()) {
        // Read flags directly from shared memory (live, zero-copy)
        uint32_t flags = sharedState_.ReadFlags();
        if (!(flags & SharedFlags::ENGINE_ENABLED)) return false;

        // [Checkpoint: TSF_ACTIVE] Only process keys when foreground app is in TSF list
        // EXE sets this flag on foreground change — prevents double-processing with hook
        bool newTsfActive = (flags & SharedFlags::TSF_ACTIVE) != 0;
        if (newTsfActive != tsfActive_) {
            tsfActive_ = newTsfActive;
            TSF_LOG(L"[Checkpoint] TSF_ACTIVE: %s", tsfActive_ ? L"ON (processing keys)" : L"OFF (passthrough)");
        }
        if (!tsfActive_) return false;

        // Detect V/E mode changes (e.g. from EXE hotkey) and refresh icon
        bool newVietnameseMode = (flags & SharedFlags::VIETNAMESE_MODE) != 0;
        if (newVietnameseMode != vietnameseMode_) {
            vietnameseMode_ = newVietnameseMode;
            if (langBarButton_) langBarButton_->Refresh();
        }

        if (!vietnameseMode_) return false;
    } else {
        // SharedState not available = EXE not running → pass all keys through
        return false;
    }

    // Auto-cap is now driven by ShouldAutoCapitalize() which peeks the document
    // on each A-Z keystroke — no keystroke-history state machine needed.

    // 1. NEVER intercept if any modifier (except Shift) is down.
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    if (ctrl || alt || win) {
        return false;
    }

    bool engineHasComp = engine_->Count() > 0;

    // 2. We want A-Z keys for typing processing
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        return true;
    }

    // 3. VNI/Combined: digit keys 1-9 for tone/modifier (only with pending composition)
    if (IsVniDigitKey(vkCode) && engineHasComp) {
        return true;
    }

    // 4. We handle Backspace ONLY if we have internal content.
    if (vkCode == VK_BACK) {
        return engineHasComp;
    }

    // 5. Space handling depends on the app
    if (vkCode == VK_SPACE) {
        if (isScintillaApp_) {
            // For Scintilla apps: don't claim space, let it trigger commit via "non-handled key" path
            // and pass through naturally
            return false;
        }
        // For other apps: claim space if we have composition
        return engineHasComp;
    }

    // 6. For Enter and all others, let the app handle it (we'll commit in OnTestKeyDown)
    return false;
}

void EngineController::RequestEditSession(ITfContext* pContext, EditSession* pEditSession) {
    if (pContext == nullptr || pEditSession == nullptr) return;

    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->RequestEditSession(
        clientId_,
        pEditSession,
        TF_ES_SYNC | TF_ES_READWRITE,
        &hrSession
    );

    if (FAILED(hr)) {
        TSF_LOG(L"RequestEditSession request failed");
    } else if (FAILED(hrSession)) {
        TSF_LOG(L"Edit session execution failed");
    }
}

bool EngineController::HandleKey(ITfContext* pContext, UINT vkCode) {
    // 1. Handle Backspace (only if we have content, as decided by WantKey)
    if (vkCode == VK_BACK) {
        // Revive path: engine was pre-seeded in OnTestKeyDown via
        // PrepareBackspaceRevive (and passed the English-word gate). Wire up
        // composition covering the cached range, then apply backspace.
        if (HasPendingRevive()) {
            auto* pSession = new ReviveCompositionEditSession(
                pContext, &compositionMgr_, engine_.get(),
                pendingReviveWord_, pendingReviveRange_);
            RequestEditSession(pContext, pSession);
            pSession->Release();
            ClearPendingRevive();
            return true;
        }
        ProcessBackspace(pContext);
        return true;
    }

    // 2. Handle Space (only reaches here for non-Scintilla apps, as decided by WantKey)
    if (vkCode == VK_SPACE) {
        // For non-Scintilla apps: append space to committed text
        CommitWithChar(pContext, L' ');
        return true;  // Eat space
    }

    // 3. Check if character key (A-Z)
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool upper = shift != capsLock;  // XOR: Shift inverts Caps Lock
        wchar_t ch = static_cast<wchar_t>(vkCode);
        if (!upper) ch = towlower(ch);

        // At a new-composition boundary: first try Type-revive (extend an
        // existing Vietnamese word before caret), and if that doesn't fire,
        // apply auto-capitalize based on document state.
        //   "tét gõ|" + 'f' → "tét [gò]"       (revive wins, no auto-cap)
        //   "b.|" + 'a' → "b.A"                (auto-cap)
        //   empty doc + 'a' → "A"              (auto-cap: cursor at doc start)
        if (engine_->Count() == 0 && !compositionMgr_.IsComposing() && !isScintillaApp_) {
            // Single READ session for both revive check and auto-cap check
            auto* pInspect = new InspectPrecedingTextEditSession(pContext);
            HRESULT hrSession = S_OK;
            pContext->RequestEditSession(clientId_, pInspect, TF_ES_SYNC | TF_ES_READ, &hrSession);

            std::wstring word = pInspect->Word();
            CComPtr<ITfRange> wordRange;
            wordRange.Attach(pInspect->DetachWordRange());
            bool shouldAutoCap = pInspect->ShouldAutoCap();
            pInspect->Release();

            // Try revive if Vietnamese word found
            if (!word.empty() && wordRange) {
                auto tempEngine = EngineFactory::Create(config_);
                if (tempEngine && tempEngine->SeedFromText(word) && !tempEngine->IsEnglishWord()) {
                    auto* pRevive = new ReviveAndTypeEditSession(
                        pContext, &compositionMgr_, engine_.get(), word, wordRange, ch);
                    RequestEditSession(pContext, pRevive);
                    pRevive->Release();
                    TSF_LOG(L"HandleKey: revive '%ls' + '%lc'", word.c_str(), ch);
                    return true;
                }
            }

            // Auto-cap if revive didn't happen
            if (config_.autoCaps && shouldAutoCap) {
                ch = towupper(ch);
                TSF_LOG(L"HandleKey: auto-cap → '%lc'", ch);
            }
        }

        TSF_LOG(L"HandleKey: pushing char '%c'", ch);
        engine_->PushChar(ch);
        
        std::wstring composition = engine_->Peek();
        TSF_LOG(L"HandleKey: got composition, starting/updating");

        if (!compositionMgr_.IsComposing()) {
            // Start new composition
            TSF_LOG(L"HandleKey: Starting new composition");
            auto* pSession = new StartCompositionEditSession(pContext, &compositionMgr_, composition);
            RequestEditSession(pContext, pSession);
            pSession->Release();

            // If composition failed to start, reset engine to stay in sync
            if (!compositionMgr_.IsComposing()) {
                TSF_LOG(L"HandleKey: Composition failed, resetting engine");
                engine_->Reset();
                return false;  // Let the key pass through
            }
        } else {
            // Update existing composition
            TSF_LOG(L"HandleKey: Updating composition");
            auto* pSession = new UpdateCompositionEditSession(pContext, &compositionMgr_, composition);
            RequestEditSession(pContext, pSession);
            pSession->Release();
        }

        TSF_LOG(L"Key processed, composition updated");
        return true;
    }

    // 4. VNI/Combined: digit keys 1-9 → push to engine, update composition
    if (IsVniDigitKey(vkCode) && engine_->Count() > 0) {
        wchar_t ch = static_cast<wchar_t>(vkCode);  // VK '1'-'9' = 0x31-0x39 = L'1'-L'9'
        TSF_LOG(L"HandleKey: pushing VNI digit '%c'", ch);
        engine_->PushChar(ch);

        std::wstring composition = engine_->Peek();
        if (compositionMgr_.IsComposing()) {
            auto* pSession = new UpdateCompositionEditSession(pContext, &compositionMgr_, composition);
            RequestEditSession(pContext, pSession);
            pSession->Release();
        }
        return true;
    }

    return false;
}

void EngineController::ProcessBackspace(ITfContext* pContext) {
    engine_->Backspace();

    if (engine_->Count() > 0) {
        std::wstring composition = engine_->Peek();
        auto* pSession = new UpdateCompositionEditSession(pContext, &compositionMgr_, composition);
        RequestEditSession(pContext, pSession);
        pSession->Release();
    } else {
        // Composition empty - clear text and end composition (delete all chars)
        auto* pSession = new CommitEditSession(pContext, &compositionMgr_, L"");
        RequestEditSession(pContext, pSession);
        pSession->Release();
        TSF_LOG(L"Backspace: composition cleared");
    }
}

void EngineController::Commit(ITfContext* pContext) {
    // VietType pattern: Get committed text, then request edit session, then reset engine.
    // This ensures TSF state and engine state are synchronized atomically.
    std::wstring committed = engine_->Commit();

    // Commit: set final text and end composition in one atomic operation
    auto* pSession = new CommitEditSession(pContext, &compositionMgr_, committed);
    RequestEditSession(pContext, pSession);
    pSession->Release();

    // Reset engine state AFTER the edit session completes (synchronous)
    engine_->Reset();

    TSF_LOG(L"Commit called, text='%ls'", committed.c_str());
}

void EngineController::CommitWithChar(ITfContext* pContext, wchar_t appendChar) {
    // VietType pattern: Get committed text, then request edit session, then reset engine.
    std::wstring committed = engine_->Commit();

    // Append the commit character (e.g., space) if provided
    if (appendChar != L'\0') {
        committed += appendChar;
    }

    // Commit: set final text and end composition in one atomic operation
    auto* pSession = new CommitEditSession(pContext, &compositionMgr_, committed);
    RequestEditSession(pContext, pSession);
    pSession->Release();

    // Reset engine state AFTER the edit session completes (synchronous)
    engine_->Reset();

    TSF_LOG(L"CommitWithChar called, text='%ls'", committed.c_str());
}

void EngineController::Reset() {
    engine_->Reset();
    compositionMgr_.TerminateComposition();
}

void EngineController::DetectScintillaApp() {
    // Cache Scintilla detection — called on context change, not per-keystroke.
    HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr) { isScintillaApp_ = false; return; }

    wchar_t className[256] = {0};
    if (GetClassNameW(hwnd, className, 256) > 0) {
        if (wcsstr(className, L"Notepad++") != nullptr) {
            isScintillaApp_ = true; return;
        }
    }

    HWND hwndFocus = GetFocus();
    if (hwndFocus != nullptr) {
        if (GetClassNameW(hwndFocus, className, 256) > 0) {
            if (wcsstr(className, L"Scintilla") != nullptr) {
                isScintillaApp_ = true; return;
            }
        }
    }

    isScintillaApp_ = false;
}

bool EngineController::CheckConfigEvent() {
    // Initialize event if not already done
    if (!configEvent_.IsValid()) {
        configEvent_.Initialize();
    }

    // Non-blocking check for signal
    if (!configEvent_.Wait(0)) {
        return false;  // No signal
    }

    // Config changed - read from SharedState
    TSF_LOG(L"Config event received, checking SharedState");

    if (!sharedState_.IsConnected()) {
        // Try to open SharedState if not connected
        if (!sharedState_.OpenReadWrite()) {
            TSF_LOG(L"CheckConfigEvent: SharedState not available");
            return false;
        }
    }

    SharedState state = sharedState_.Read();
    if (!state.IsValid()) {
        TSF_LOG(L"CheckConfigEvent: SharedState invalid");
        return false;
    }

    // Check if epoch changed (config actually updated)
    if (state.epoch == lastEpoch_) {
        TSF_LOG(L"CheckConfigEvent: epoch unchanged, skipping reload");
        return false;
    }

    // Apply new config
    TSF_LOG(L"Config changed: epoch %u -> %u", lastEpoch_, state.epoch);
    lastEpoch_ = state.epoch;
    ApplySharedState(state);

    return true;
}

void EngineController::RefreshFlags() {
    DetectScintillaApp();
    if (!sharedState_.IsConnected()) {
        // Try to reconnect (EXE may have restarted)
        if (!sharedState_.OpenReadWrite()) {
            engineEnabled_ = false;
            return;
        }
        TSF_LOG(L"Reconnected to SharedState");
    }

    SharedState state = sharedState_.Read();
    if (state.IsValid()) {
        bool wasEnabled = engineEnabled_;
        bool wasVietnamese = vietnameseMode_;
        engineEnabled_ = (state.flags & SharedFlags::ENGINE_ENABLED) != 0;
        vietnameseMode_ = (state.flags & SharedFlags::VIETNAMESE_MODE) != 0;

        if (!wasEnabled && engineEnabled_) {
            TSF_LOG(L"Engine re-enabled (app started)");
            ApplySharedState(state);
        } else if (wasEnabled && !engineEnabled_) {
            TSF_LOG(L"Engine disabled (app exited)");
        }

        // Refresh icon if Vietnamese mode changed (e.g. EXE hotkey toggled while bg)
        if (wasVietnamese != vietnameseMode_ && langBarButton_) {
            langBarButton_->Refresh();
        }
    } else {
        engineEnabled_ = false;
    }
}

void EngineController::ApplySharedState(const SharedState& state) {
    // Update runtime flags
    engineEnabled_ = (state.flags & SharedFlags::ENGINE_ENABLED) != 0;
    vietnameseMode_ = (state.flags & SharedFlags::VIETNAMESE_MODE) != 0;

    // Validate inputMethod (valid range: 0–2) before casting to enum
    InputMethod newMethod = InputMethod::Telex;  // safe default
    if (state.inputMethod <= 2) {
        newMethod = static_cast<InputMethod>(state.inputMethod);
    } else {
        TSF_LOG(L"[EngineController] Invalid inputMethod %d from shared state, using Telex",
                state.inputMethod);
    }

    // Validate optimizeLevel (valid range: 0–2)
    uint8_t optimizeLevel = 0;
    if (state.optimizeLevel <= 2) {
        optimizeLevel = state.optimizeLevel;
    }

    config_.inputMethod = newMethod;
    config_.spellCheckEnabled = state.spellCheck != 0;
    config_.optimizeLevel = optimizeLevel;
    DecodeFeatureFlags(state.GetFeatureFlags(), config_);

    // Recreate engine with updated config (engine stores a copy of TypingConfig,
    // so we must recreate it whenever any config field changes)
    // Commit any pending composition before recreating
    if (engine_ && engine_->Count() > 0) {
        (void)engine_->Commit();
    }

    currentMethod_ = newMethod;
    engine_ = EngineFactory::Create(config_);
    TSF_LOG(L"Engine recreated (%s, modernOrtho=%d, allowZwjf=%d)",
            newMethod == InputMethod::VNI ? L"VNI" : L"Telex",
            config_.modernOrtho ? 1 : 0, config_.allowZwjf ? 1 : 0);
}

void EngineController::ToggleVietnameseMode() {
    sharedState_.ToggleFlag(SharedFlags::VIETNAMESE_MODE);
    // Read back actual flag to stay in sync (avoids TOCTOU with EXE toggling)
    vietnameseMode_ = (sharedState_.ReadFlags() & SharedFlags::VIETNAMESE_MODE) != 0;
    if (langBarButton_) {
        langBarButton_->Refresh();
    }
    TSF_LOG(L"ToggleVietnameseMode: now %s", vietnameseMode_ ? L"Vietnamese" : L"English");
}

bool EngineController::InitLanguageBar(ITfThreadMgr* pThreadMgr) {
    if (!pThreadMgr) return false;

    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;
    HRESULT hr = pThreadMgr->QueryInterface(
        IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&pLangBarItemMgr));
    if (FAILED(hr) || !pLangBarItemMgr) {
        TSF_LOG(L"InitLanguageBar: failed to get ITfLangBarItemMgr");
        return false;
    }

    langBarButton_ = new LanguageBarButton();
    if (!langBarButton_->Initialize(this, pLangBarItemMgr)) {
        TSF_LOG(L"InitLanguageBar: button initialization failed");
        langBarButton_->Release();
        langBarButton_ = nullptr;
        pLangBarItemMgr->Release();
        return false;
    }

    pLangBarItemMgr->Release();
    TSF_LOG(L"InitLanguageBar: success");
    return true;
}

void EngineController::UninitLanguageBar() {
    if (langBarButton_) {
        langBarButton_->Uninitialize();
        langBarButton_->Release();
        langBarButton_ = nullptr;
    }
}

}  // namespace TSF
}  // namespace NextKey
