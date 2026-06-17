// VKey - Composition Edit Sessions
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "EditSession.h"
#include "CompositionManager.h"
#include "Define.h"
#include "core/AutoCapDecision.h"
#include "core/engine/IInputEngine.h"
#include "core/engine/VietnameseTables.h"
#include <algorithm>
#include <string>

namespace NextKey {
namespace TSF {

/// Edit session to start a new composition
class StartCompositionEditSession : public EditSession {
public:
    StartCompositionEditSession(ITfContext* pContext, CompositionManager* pMgr, const std::wstring& text)
        : EditSession(pContext), pMgr_(pMgr), text_(text) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr || pContext_ == nullptr) return E_FAIL;

        CComPtr<ITfComposition> pComposition;
        if (!pMgr_->StartComposition(pContext_, ec, &pComposition)) {
            TSF_LOG(L"StartCompositionEditSession: Failed to start composition");
            return E_FAIL;
        }

        if (!text_.empty()) {
            pMgr_->SetCompositionText(ec, text_);
        }

        TSF_LOG(L"StartCompositionEditSession: Success");
        return S_OK;
    }

private:
    CompositionManager* pMgr_;
    std::wstring text_;
};

/// Edit session to update composition text
class UpdateCompositionEditSession : public EditSession {
public:
    UpdateCompositionEditSession(ITfContext* pContext, CompositionManager* pMgr, const std::wstring& text)
        : EditSession(pContext), pMgr_(pMgr), text_(text) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr) return E_FAIL;

        if (!pMgr_->IsComposing()) {
            TSF_LOG(L"UpdateCompositionEditSession: Not composing, starting new");
            CComPtr<ITfComposition> pComposition;
            if (!pMgr_->StartComposition(pContext_, ec, &pComposition)) {
                return E_FAIL;
            }
        }

        // Replace composition text
        pMgr_->SetCompositionText(ec, text_);

        TSF_LOG(L"UpdateCompositionEditSession: Success");
        return S_OK;
    }

private:
    CompositionManager* pMgr_;
    std::wstring text_;
};

/// Edit session to end composition
class EndCompositionEditSession : public EditSession {
public:
    EndCompositionEditSession(ITfContext* pContext, CompositionManager* pMgr)
        : EditSession(pContext), pMgr_(pMgr) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr) return E_FAIL;

        pMgr_->EndComposition(ec);
        TSF_LOG(L"EndCompositionEditSession: Success");
        return S_OK;
    }

private:
    CompositionManager* pMgr_;
};

/// Edit session to commit with final text (update + end in one operation)
class CommitEditSession : public EditSession {
public:
    CommitEditSession(ITfContext* pContext, CompositionManager* pMgr, const std::wstring& finalText)
        : EditSession(pContext), pMgr_(pMgr), finalText_(finalText) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr) return E_FAIL;

        if (pMgr_->IsComposing()) {
            pMgr_->SetCompositionText(ec, finalText_);
            pMgr_->EndComposition(ec);
        } else {
            TSF_LOG(L"CommitEditSession: composition externally terminated, text dropped: %s",
                    finalText_.c_str());
        }

        return S_OK;
    }

private:
    CompositionManager* pMgr_;
    std::wstring finalText_;
};

/// Read-only edit session: fetch up to N chars preceding the caret.
/// No filtering — caller interprets the buffer (e.g. for auto-cap rules).
/// `AtDocStart()` is true when the caret can't be shifted back at all.
class ReadPrecedingCharsEditSession : public EditSession {
public:
    // Caller passes a compile-time max; buffer sized at template instantiation. 64 is
    // plenty for auto-cap (only need ~16 to skip trailing whitespace).
    static constexpr LONG MAX_CHARS = 64;

    explicit ReadPrecedingCharsEditSession(ITfContext* pContext, LONG maxChars = MAX_CHARS)
        : EditSession(pContext), maxChars_((std::min)(maxChars, MAX_CHARS)) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pContext_ == nullptr) return E_FAIL;

        TF_SELECTION sel = {};
        ULONG fetched = 0;
        HRESULT hr = pContext_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
        if (FAILED(hr) || fetched != 1 || sel.range == nullptr) return S_OK;
        CComPtr<ITfRange> pSelRange;
        pSelRange.Attach(sel.range);

        BOOL selEmpty = FALSE;
        if (FAILED(pSelRange->IsEmpty(ec, &selEmpty)) || !selEmpty) return S_OK;

        CComPtr<ITfRange> pPeek;
        if (FAILED(pSelRange->Clone(&pPeek)) || !pPeek) return S_OK;

        TF_HALTCOND haltcond = { nullptr, TF_ANCHOR_START, TF_HF_OBJECT };
        LONG shifted = 0;
        if (FAILED(pPeek->ShiftStart(ec, -maxChars_, &shifted, &haltcond))) return S_OK;
        if (shifted == 0) {
            atDocStart_ = true;  // caret sits at document start — can't move back at all
            return S_OK;
        }
        if (shifted > 0) return S_OK;  // unexpected (shift-back should never go forward)

        WCHAR buf[MAX_CHARS] = {};
        ULONG retrieved = 0;
        if (FAILED(pPeek->GetText(ec, 0, buf, -shifted, &retrieved)) || retrieved == 0) {
            return S_OK;
        }
        text_.assign(buf, retrieved);
        return S_OK;
    }

    [[nodiscard]] const std::wstring& Text() const noexcept { return text_; }
    [[nodiscard]] bool AtDocStart() const noexcept { return atDocStart_; }

private:
    LONG maxChars_;
    std::wstring text_;
    bool atDocStart_ = false;
};

/// Read-only edit session: find the Vietnamese word immediately before the caret.
/// Walks backward over up to MAX_CHARS chars, counting consecutive Vietnamese letters.
/// On success, DetachRange() transfers ownership of the word range to the caller.
class ReadPrecedingWordEditSession : public EditSession {
public:
    explicit ReadPrecedingWordEditSession(ITfContext* pContext)
        : EditSession(pContext) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pContext_ == nullptr) return E_FAIL;

        constexpr LONG MAX_CHARS = 16;

        TF_SELECTION sel = {};
        ULONG fetched = 0;
        HRESULT hr = pContext_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
        if (FAILED(hr) || fetched != 1 || sel.range == nullptr) return S_OK;

        // GetSelection transfers a ref; Attach takes ownership without extra AddRef.
        CComPtr<ITfRange> pSelRange;
        pSelRange.Attach(sel.range);

        BOOL selEmpty = FALSE;
        if (FAILED(pSelRange->IsEmpty(ec, &selEmpty)) || !selEmpty) return S_OK;

        CComPtr<ITfRange> pPeek;
        if (FAILED(pSelRange->Clone(&pPeek)) || !pPeek) return S_OK;

        TF_HALTCOND haltcond = { nullptr, TF_ANCHOR_START, TF_HF_OBJECT };
        LONG shifted = 0;
        if (FAILED(pPeek->ShiftStart(ec, -MAX_CHARS, &shifted, &haltcond)) || shifted >= 0) {
            return S_OK;
        }
        LONG availChars = -shifted;

        WCHAR buf[MAX_CHARS] = {};
        ULONG retrieved = 0;
        HRESULT hrG = pPeek->GetText(ec, 0, buf, (std::min)(availChars, MAX_CHARS), &retrieved);
        if (FAILED(hrG) || retrieved == 0) return S_OK;

        LONG wordlen = 0;
        for (LONG i = static_cast<LONG>(retrieved) - 1; i >= 0; --i) {
            wchar_t base = 0;
            int modIdx = -1, toneIdx = -1;
            bool isUpper = false;
            if (DecomposeVietChar(buf[i], base, modIdx, toneIdx, isUpper)) {
                ++wordlen;
            } else {
                break;
            }
        }

        if (wordlen == 0) return S_OK;

        LONG shifted2 = 0;
        if (FAILED(pSelRange->ShiftStart(ec, -wordlen, &shifted2, &haltcond))) return S_OK;

        word_.assign(&buf[retrieved - wordlen], wordlen);
        pRange_ = pSelRange;  // CComPtr assignment — AddRefs for the member.
        TSF_LOG(L"ReadPrecedingWord: found '%ls' len=%ld", word_.c_str(), wordlen);
        return S_OK;
    }

    [[nodiscard]] bool Found() const noexcept { return pRange_ != nullptr; }
    [[nodiscard]] const std::wstring& Word() const noexcept { return word_; }

    /// Transfer ownership of the range to the caller. Returns nullptr if no word found.
    ITfRange* DetachRange() noexcept { return pRange_.Detach(); }

private:
    std::wstring word_;
    CComPtr<ITfRange> pRange_;
};

/// Combined read session: extracts Vietnamese word AND checks auto-cap in one pass.
/// Replaces separate ReadPrecedingWordEditSession + ReadPrecedingCharsEditSession calls
/// on the HandleKey A-Z path when engine buffer is empty.
class InspectPrecedingTextEditSession : public EditSession {
public:
    explicit InspectPrecedingTextEditSession(ITfContext* pContext)
        : EditSession(pContext) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pContext_ == nullptr) return E_FAIL;

        constexpr LONG MAX_CHARS = 64;

        TF_SELECTION sel = {};
        ULONG fetched = 0;
        HRESULT hr = pContext_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
        if (FAILED(hr) || fetched != 1 || sel.range == nullptr) return S_OK;
        CComPtr<ITfRange> pSelRange;
        pSelRange.Attach(sel.range);

        BOOL selEmpty = FALSE;
        if (FAILED(pSelRange->IsEmpty(ec, &selEmpty)) || !selEmpty) return S_OK;

        CComPtr<ITfRange> pPeek;
        if (FAILED(pSelRange->Clone(&pPeek)) || !pPeek) return S_OK;

        TF_HALTCOND haltcond = { nullptr, TF_ANCHOR_START, TF_HF_OBJECT };
        LONG shifted = 0;
        if (FAILED(pPeek->ShiftStart(ec, -MAX_CHARS, &shifted, &haltcond))) return S_OK;

        if (shifted == 0) {
            atDocStart_ = true;
            shouldAutoCap_ = true;
            return S_OK;
        }
        if (shifted > 0) return S_OK;

        LONG availChars = -shifted;
        WCHAR buf[MAX_CHARS] = {};
        ULONG len = 0;
        if (FAILED(pPeek->GetText(ec, 0, buf, availChars, &len)) || len == 0) return S_OK;

        // Extract Vietnamese word (walk backward using DecomposeVietChar)
        LONG wordLen = 0;
        for (LONG i = static_cast<LONG>(len) - 1; i >= 0; --i) {
            wchar_t base = 0;
            int modIdx = -1, toneIdx = -1;
            bool isUpper = false;
            if (DecomposeVietChar(buf[i], base, modIdx, toneIdx, isUpper)) {
                ++wordLen;
            } else {
                break;
            }
        }

        if (wordLen > 0) {
            word_.assign(&buf[len - wordLen], wordLen);
            CComPtr<ITfRange> pWordRange;
            if (SUCCEEDED(pSelRange->Clone(&pWordRange)) && pWordRange) {
                LONG shifted2 = 0;
                if (SUCCEEDED(pWordRange->ShiftStart(ec, -wordLen, &shifted2, &haltcond))) {
                    wordRange_ = pWordRange;
                }
            }
        }

        // Auto-cap rule extracted to core/AutoCapDecision.h for Linux GTest
        // coverage (the buffer comes from a Win32 edit session here, but the
        // decision is pure CPU work over a wchar_t span).
        shouldAutoCap_ = ComputeShouldAutoCap(buf, len);

        return S_OK;
    }

    [[nodiscard]] const std::wstring& Word() const noexcept { return word_; }
    [[nodiscard]] ITfRange* DetachWordRange() noexcept { return wordRange_.Detach(); }
    [[nodiscard]] bool ShouldAutoCap() const noexcept { return shouldAutoCap_; }
    [[nodiscard]] bool AtDocStart() const noexcept { return atDocStart_; }

private:
    std::wstring word_;
    CComPtr<ITfRange> wordRange_;
    bool shouldAutoCap_ = false;
    bool atDocStart_ = false;
};

/// Revive-composition edit session: starts a composition over an existing range that
/// covers a previously-committed Vietnamese word, seeds the engine from that word,
/// then deletes the last char (since this fires from VK_BACK). The resulting
/// composition text is the seeded word minus one char.
///
/// Expected caller flow:
///   1. ReadPrecedingWordEditSession returns (word, range covering word including the char to delete)
///   2. Engine state is empty
///   3. This session: compose covers range, SetText replaces range with word-minus-last-char
class ReviveCompositionEditSession : public EditSession {
public:
    ReviveCompositionEditSession(ITfContext* pContext, CompositionManager* pMgr,
                                 IInputEngine* pEngine, const std::wstring& word,
                                 ITfRange* pRange)
        : EditSession(pContext), pMgr_(pMgr), pEngine_(pEngine), word_(word),
          pRange_(pRange) {  // CComPtr assignment AddRefs automatically
    }

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr || pEngine_ == nullptr || pContext_ == nullptr || !pRange_) {
            return E_FAIL;
        }
        if (word_.empty()) return E_FAIL;

        // Seed engine_ fresh (English gate already passed upstream via a temp engine).
        if (!pEngine_->SeedFromText(word_)) {
            TSF_LOG(L"ReviveCompositionEditSession: SeedFromText failed on '%ls'",
                    word_.c_str());
            return E_FAIL;
        }

        CComPtr<ITfComposition> pComposition;
        if (!pMgr_->StartComposition(pContext_, ec, &pComposition)) {
            TSF_LOG(L"ReviveCompositionEditSession: StartComposition failed");
            pEngine_->Reset();
            return E_FAIL;
        }

        HRESULT hr = pComposition->ShiftStart(ec, pRange_);
        if (FAILED(hr)) {
            TSF_LOG(L"ReviveCompositionEditSession: ShiftStart failed hr=0x%08X", hr);
            pEngine_->Reset();
            pMgr_->EndComposition(ec);
            return hr;
        }

        pEngine_->Backspace();
        const std::wstring& composed = pEngine_->Peek();
        pMgr_->SetCompositionText(ec, composed);

        if (pEngine_->Count() == 0) {
            pMgr_->EndComposition(ec);
        }

        TSF_LOG(L"ReviveCompositionEditSession: composition='%ls'", composed.c_str());
        return S_OK;
    }

private:
    CompositionManager* pMgr_;
    IInputEngine* pEngine_;
    std::wstring word_;
    CComPtr<ITfRange> pRange_;
};

/// Revive-and-type edit session: user typed a character while engine was empty
/// and cursor sat right after a Vietnamese word. Starts composition over the word,
/// seeds the engine, then PushChar'es the incoming char — so e.g. "gõ" + 'f'
/// re-enters composition and produces "gò" (grave tone replaces tilde).
class ReviveAndTypeEditSession : public EditSession {
public:
    ReviveAndTypeEditSession(ITfContext* pContext, CompositionManager* pMgr,
                             IInputEngine* pEngine, const std::wstring& word,
                             ITfRange* pRange, wchar_t ch)
        : EditSession(pContext), pMgr_(pMgr), pEngine_(pEngine),
          word_(word), pRange_(pRange), ch_(ch) {
    }

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pMgr_ == nullptr || pEngine_ == nullptr || pContext_ == nullptr
            || !pRange_ || word_.empty() || ch_ == 0) {
            return E_FAIL;
        }

        if (!pEngine_->SeedFromText(word_)) {
            TSF_LOG(L"ReviveAndTypeEditSession: SeedFromText failed on '%ls'",
                    word_.c_str());
            return E_FAIL;
        }

        CComPtr<ITfComposition> pComposition;
        if (!pMgr_->StartComposition(pContext_, ec, &pComposition)) {
            TSF_LOG(L"ReviveAndTypeEditSession: StartComposition failed");
            pEngine_->Reset();
            return E_FAIL;
        }

        HRESULT hr = pComposition->ShiftStart(ec, pRange_);
        if (FAILED(hr)) {
            TSF_LOG(L"ReviveAndTypeEditSession: ShiftStart failed hr=0x%08X", hr);
            pEngine_->Reset();
            pMgr_->EndComposition(ec);
            return hr;
        }

        pEngine_->PushChar(ch_);
        const std::wstring& composed = pEngine_->Peek();
        pMgr_->SetCompositionText(ec, composed);

        if (pEngine_->Count() == 0) {
            pMgr_->EndComposition(ec);
        }

        TSF_LOG(L"ReviveAndTypeEditSession: '%ls' + '%lc' → '%ls'",
                word_.c_str(), ch_, composed.c_str());
        return S_OK;
    }

private:
    CompositionManager* pMgr_;
    IInputEngine* pEngine_;
    std::wstring word_;
    CComPtr<ITfRange> pRange_;
    wchar_t ch_;
};

/// Edit session to check if the selection is non-empty (for autocomplete detection)
class SelectionCheckEditSession : public EditSession {
public:
    SelectionCheckEditSession(ITfContext* pContext, bool* pHasSelection)
        : EditSession(pContext), pHasSelection_(pHasSelection) {
        if (pHasSelection_) *pHasSelection_ = false;
    }

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pContext_ == nullptr || pHasSelection_ == nullptr) return E_FAIL;
        *pHasSelection_ = false;

        TF_SELECTION sel = {};
        ULONG fetched = 0;
        HRESULT hr = pContext_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
        if (SUCCEEDED(hr) && fetched == 1 && sel.range != nullptr) {
            CComPtr<ITfRange> pSelRange;
            pSelRange.Attach(sel.range);
            BOOL isEmpty = FALSE;
            if (SUCCEEDED(pSelRange->IsEmpty(ec, &isEmpty))) {
                *pHasSelection_ = (isEmpty == FALSE);
            }
        }
        return S_OK;
    }

private:
    bool* pHasSelection_;
};

}  // namespace TSF
}  // namespace NextKey
