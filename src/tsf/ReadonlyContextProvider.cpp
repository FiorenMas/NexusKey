// VKey - Readonly Context Provider Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "stdafx.h"
#include "ReadonlyContextProvider.h"
#include "InputScopeChecker.h"
#include "EditSession.h"
#include "Define.h"
#include "core/ipc/SharedState.h"
#include <InputScope.h>

namespace NextKey {
namespace TSF {

namespace {

// Per-OnEndEdit chars to scan. 64 matches ReadPrecedingCharsEditSession's
// MAX_CHARS and is enough to walk back over whitespace after sentence-end punct.
constexpr LONG kReadbackChars = 64;

// Inline context-block check for use with an existing ec (OnEndEdit is sync).
// Mirrors InputScopeCheckSession::DoEditSession; separated so we can run without
// spinning up a new edit session (we already hold ec).
bool IsContextBlockedInline(ITfContext* pContext, TfEditCookie ec) noexcept {
    if (pContext == nullptr) return true;  // defensive: treat null as blocked

    // 1. GUID_COMPARTMENT_KEYBOARD_DISABLED
    ITfCompartmentMgr* pCompMgr = nullptr;
    if (SUCCEEDED(pContext->QueryInterface(IID_ITfCompartmentMgr, (void**)&pCompMgr)) && pCompMgr) {
        ITfCompartment* pComp = nullptr;
        if (SUCCEEDED(pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &pComp)) && pComp) {
            VARIANT var; VariantInit(&var);
            if (SUCCEEDED(pComp->GetValue(&var)) && var.vt == VT_I4 && var.lVal != 0) {
                VariantClear(&var);
                pComp->Release();
                pCompMgr->Release();
                return true;
            }
            VariantClear(&var);
            pComp->Release();
        }
        pCompMgr->Release();
    }

    // 2. Input scopes — password/PIN/email
    ITfReadOnlyProperty* pProp = nullptr;
    if (FAILED(pContext->GetAppProperty(kGuidPropInputScope, &pProp)) || !pProp) return false;

    TF_SELECTION sel = {};
    ULONG fetched = 0;
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched == 0) {
        pProp->Release();
        return false;
    }

    VARIANT var; VariantInit(&var);
    HRESULT hr = pProp->GetValue(ec, sel.range, &var);
    sel.range->Release();
    pProp->Release();
    if (FAILED(hr) || var.vt != VT_UNKNOWN || !var.punkVal) {
        VariantClear(&var);
        return false;
    }

    ITfInputScope* pInputScope = nullptr;
    hr = var.punkVal->QueryInterface(IID_ITfInputScope, (void**)&pInputScope);
    VariantClear(&var);
    if (FAILED(hr) || !pInputScope) return false;

    InputScope* pScopes = nullptr;
    UINT scopeCount = 0;
    hr = pInputScope->GetInputScopes(&pScopes, &scopeCount);
    pInputScope->Release();
    if (FAILED(hr) || !pScopes) return false;

    bool blocked = false;
    for (UINT i = 0; i < scopeCount && !blocked; ++i) {
        switch (pScopes[i]) {
            case IS_PASSWORD:
            case IS_NUMERIC_PASSWORD:
            case IS_NUMERIC_PIN:
            case IS_ALPHANUMERIC_PIN:
            case IS_ALPHANUMERIC_PIN_SET:
            case IS_EMAIL_USERNAME:
            case IS_EMAIL_SMTPEMAILADDRESS:
            case IS_EMAILNAME_OR_ADDRESS:
            case IS_LOGINNAME:
                blocked = true;
                break;
            default:
                break;
        }
    }
    CoTaskMemFree(pScopes);
    return blocked;
}

// Read up to `bufSize` chars immediately before the caret using the provided ec.
// Returns the number of chars written to buf. len==0 covers both "caret at doc
// start" (ShiftStart returned 0) and "read failed" — DeriveAnchorFromPreceding
// treats both identically (conservative start-of-everything).
ULONG ReadPrecedingCharsInline(ITfContext* pContext, TfEditCookie ec,
                               wchar_t* buf, ULONG bufSize) noexcept {
    if (pContext == nullptr || buf == nullptr || bufSize == 0) return 0;

    TF_SELECTION sel = {};
    ULONG fetched = 0;
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched))
        || fetched != 1 || sel.range == nullptr) {
        return 0;
    }
    CComPtr<ITfRange> pSelRange;
    pSelRange.Attach(sel.range);

    BOOL selEmpty = FALSE;
    if (FAILED(pSelRange->IsEmpty(ec, &selEmpty)) || !selEmpty) return 0;

    CComPtr<ITfRange> pPeek;
    if (FAILED(pSelRange->Clone(&pPeek)) || !pPeek) return 0;

    TF_HALTCOND haltcond = { nullptr, TF_ANCHOR_START, TF_HF_OBJECT };
    LONG shifted = 0;
    if (FAILED(pPeek->ShiftStart(ec, -static_cast<LONG>(bufSize), &shifted, &haltcond))) return 0;
    if (shifted >= 0) return 0;  // 0 = doc start (no chars to read), >0 = unexpected

    ULONG retrieved = 0;
    if (FAILED(pPeek->GetText(ec, 0, buf, -shifted, &retrieved))) return 0;
    return retrieved;
}

}  // namespace

// ============================================================================
// Construction / destruction
// ============================================================================

ReadonlyContextProvider::ReadonlyContextProvider(SharedStateManager* pSharedState) noexcept
    : pSharedState_(pSharedState) {
    TSF_LOG(L"ReadonlyContextProvider created");
}

ReadonlyContextProvider::~ReadonlyContextProvider() {
    Unadvise();
    TSF_LOG(L"ReadonlyContextProvider destroyed");
}

// ============================================================================
// IUnknown
// ============================================================================

IFACEMETHODIMP ReadonlyContextProvider::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppvObj = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfTextEditSink)) {
        *ppvObj = static_cast<ITfTextEditSink*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) ReadonlyContextProvider::AddRef() {
    return InterlockedIncrement(&refCount_);
}

IFACEMETHODIMP_(ULONG) ReadonlyContextProvider::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    if (count == 0) delete this;
    return count;
}

// ============================================================================
// Advise / Unadvise
// ============================================================================

bool ReadonlyContextProvider::Advise(ITfThreadMgr* pThreadMgr, TfClientId clientId) {
    (void)clientId;  // unused — OnEndEdit provides the edit cookie
    if (pThreadMgr == nullptr) return false;
    if (threadMgrCookie_ != TF_INVALID_COOKIE) return true;  // already advised

    pThreadMgr_ = pThreadMgr;

    ITfSource* pSource = nullptr;
    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource);
    if (FAILED(hr) || !pSource) {
        TSF_LOG(L"ReadonlyContextProvider: QI(ITfSource) failed hr=0x%08X", hr);
        return false;
    }

    hr = pSource->AdviseSink(IID_ITfThreadMgrEventSink,
                             static_cast<ITfThreadMgrEventSink*>(this),
                             &threadMgrCookie_);
    pSource->Release();
    if (FAILED(hr)) {
        TSF_LOG(L"ReadonlyContextProvider: AdviseSink(ThreadMgr) failed hr=0x%08X", hr);
        threadMgrCookie_ = TF_INVALID_COOKIE;
        return false;
    }

    // Prime with current focus if any.
    CComPtr<ITfDocumentMgr> pDocMgr;
    if (SUCCEEDED(pThreadMgr->GetFocus(&pDocMgr)) && pDocMgr) {
        isFocused_ = true;
        AdviseEditSink(pDocMgr);
    }

    TSF_LOG(L"ReadonlyContextProvider advised");
    return true;
}

void ReadonlyContextProvider::Unadvise() {
    UnadviseEditSink();

    if (threadMgrCookie_ != TF_INVALID_COOKIE && pThreadMgr_) {
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(pThreadMgr_->QueryInterface(IID_ITfSource, (void**)&pSource)) && pSource) {
            pSource->UnadviseSink(threadMgrCookie_);
            pSource->Release();
        }
        threadMgrCookie_ = TF_INVALID_COOKIE;
    }

    pThreadMgr_ = nullptr;
    isFocused_  = false;
}

void ReadonlyContextProvider::AdviseEditSink(ITfDocumentMgr* pDocMgr) {
    UnadviseEditSink();  // drop any previous
    if (pDocMgr == nullptr) return;

    CComPtr<ITfContext> pContext;
    if (FAILED(pDocMgr->GetTop(&pContext)) || !pContext) return;

    ITfSource* pSource = nullptr;
    if (FAILED(pContext->QueryInterface(IID_ITfSource, (void**)&pSource)) || !pSource) return;

    HRESULT hr = pSource->AdviseSink(IID_ITfTextEditSink,
                                     static_cast<ITfTextEditSink*>(this),
                                     &editCookie_);
    pSource->Release();
    if (FAILED(hr)) {
        TSF_LOG(L"ReadonlyContextProvider: AdviseSink(TextEdit) failed hr=0x%08X", hr);
        editCookie_ = TF_INVALID_COOKIE;
        return;
    }
    pAdvisedContext_ = pContext;
}

void ReadonlyContextProvider::UnadviseEditSink() {
    if (editCookie_ != TF_INVALID_COOKIE && pAdvisedContext_) {
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(pAdvisedContext_->QueryInterface(IID_ITfSource, (void**)&pSource)) && pSource) {
            pSource->UnadviseSink(editCookie_);
            pSource->Release();
        }
        editCookie_ = TF_INVALID_COOKIE;
    }
    pAdvisedContext_.Release();
}

// ============================================================================
// ITfThreadMgrEventSink
// ============================================================================

IFACEMETHODIMP ReadonlyContextProvider::OnSetFocus(ITfDocumentMgr* pDocMgrFocus,
                                                   ITfDocumentMgr* pDocMgrPrevFocus) {
    (void)pDocMgrPrevFocus;
    if (pDocMgrFocus == nullptr) {
        // Losing focus — invalidate anchor so readers (Hook in another process
        // that just gained focus) don't see stale doc truth from this app before
        // their own TSF has time to push fresh data.
        if (pSharedState_ && pSharedState_->IsConnected()) {
            HookContextAnchor cleared{};  // isAvailable=0, all flags=0
            pSharedState_->WriteAnchor(cleared);
            lastWritten_ = cleared;
        }
        UnadviseEditSink();
        isFocused_ = false;
    } else {
        isFocused_ = true;
        AdviseEditSink(pDocMgrFocus);
        // First OnEndEdit on user edit will push fresh anchor. Until then the
        // anchor we just wrote (isAvailable=0, from the previous focus-out) tells
        // Hook to fall back to its keystroke state machine — safe default.
    }
    return S_OK;
}

// ============================================================================
// ITfTextEditSink
// ============================================================================

IFACEMETHODIMP ReadonlyContextProvider::OnEndEdit(ITfContext* pContext,
                                                  TfEditCookie ecReadOnly,
                                                  ITfEditRecord* pEditRecord) {
    (void)pEditRecord;  // phase 1 ignores; phase 2 may use GetSelectionStatus
    if (!isFocused_) return S_OK;
    if (!IsReadonlyModeActive()) return S_OK;
    UpdateAnchor(pContext, ecReadOnly);
    return S_OK;
}

// ============================================================================
// Core update logic
// ============================================================================

bool ReadonlyContextProvider::IsReadonlyModeActive() const noexcept {
    if (pSharedState_ == nullptr || !pSharedState_->IsConnected()) return false;
    const uint32_t flags = pSharedState_->ReadFlags();
    // Gate on TSF_READONLY only. ENGINE_ENABLED is Hook-side; if Hook is off,
    // it won't read the anchor — writing it is still harmless.
    return (flags & SharedFlags::TSF_READONLY) != 0;
}

namespace {
/// Payload-only equality (ignores generation — that's the writer's private counter).
bool AnchorPayloadEquals(const HookContextAnchor& a, const HookContextAnchor& b) noexcept {
    if (a.isAvailable     != b.isAvailable)     return false;
    if (a.isSentenceStart != b.isSentenceStart) return false;
    if (a.isLineStart     != b.isLineStart)     return false;
    if (a.isWordStart     != b.isWordStart)     return false;
    if (a.syllableLen     != b.syllableLen)     return false;
    for (size_t k = 0; k < a.syllableLen; ++k) {
        if (a.currentSyllable[k] != b.currentSyllable[k]) return false;
    }
    return true;
}
}  // namespace

void ReadonlyContextProvider::UpdateAnchor(ITfContext* pContext, TfEditCookie ec) {
    HookContextAnchor anchor{};

    // TODO(logging): when user-facing log infra lands, instrument the two
    // "isAvailable = 0" paths below (password-blocked and read-failed) so we
    // can identify apps where readonly context mode is silently degrading to
    // the keystroke-state-machine fallback.

    // Password / blocked context → mark unavailable and bail.
    if (IsContextBlockedInline(pContext, ec)) {
        anchor.isAvailable = 0;
    } else {
        wchar_t buf[kReadbackChars] = {};
        ULONG len = ReadPrecedingCharsInline(pContext, ec, buf, kReadbackChars);

        // On Windows wchar_t is 16-bit (UTF-16); reinterpret is safe.
        static_assert(sizeof(wchar_t) == sizeof(uint16_t), "wchar_t must be UTF-16 on Windows");
        DeriveAnchorFromPreceding(reinterpret_cast<const uint16_t*>(buf),
                                  static_cast<size_t>(len), anchor);
        anchor.isAvailable = 1;
    }

    // Skip the cross-process write if the payload hasn't changed since our
    // last push — avoids bumping `generation` (and forcing reader retries on
    // another process) every keystroke within the same word.
    if (AnchorPayloadEquals(anchor, lastWritten_)) return;

    if (pSharedState_) pSharedState_->WriteAnchor(anchor);
    lastWritten_ = anchor;
}

}  // namespace TSF
}  // namespace NextKey
