// VKey - Composition Manager Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "stdafx.h"
#include "CompositionManager.h"
#include "EngineController.h"
#include "DisplayAttribute.h"
#include "Define.h"

namespace NextKey {
namespace TSF {

// IUnknown
IFACEMETHODIMP CompositionManager::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppvObj = static_cast<ITfCompositionSink*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

// ITfCompositionSink - called when app or system terminates our composition
IFACEMETHODIMP CompositionManager::OnCompositionTerminated(TfEditCookie /*ec*/, ITfComposition* pComposition) {
    TSF_LOG(L"OnCompositionTerminated called by system");
    if (pComposition == pComposition_) {
        pComposition_->Release();
        pComposition_ = nullptr;
    }

    // Always reset engine to stay in sync whenever a composition ends
    if (pEngineController_) {
        pEngineController_->Reset();
    }
    return S_OK;
}

bool CompositionManager::StartComposition(ITfContext* pContext, TfEditCookie ec, ITfComposition** ppComposition) {
    if (pContext == nullptr || ppComposition == nullptr) {
        TSF_LOG(L"StartComposition: ERROR - null context or pointer");
        return false;
    }

    // End any existing composition first
    if (pComposition_) {
        TSF_LOG(L"StartComposition: Ending existing composition first");
        EndComposition(ec);
    }

    // Get context composition interface
    ITfContextComposition* pContextComposition = nullptr;
    HRESULT hr = pContext->QueryInterface(IID_ITfContextComposition, (void**)&pContextComposition);
    if (FAILED(hr) || pContextComposition == nullptr) {
        TSF_LOG(L"StartComposition: Failed ITfContextComposition, hr=0x%08X", hr);
        return false;
    }

    // Get insertion interface
    ITfInsertAtSelection* pInsertAtSelection = nullptr;
    hr = pContext->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsertAtSelection);
    if (FAILED(hr) || pInsertAtSelection == nullptr) {
        pContextComposition->Release();
        TSF_LOG(L"StartComposition: Failed ITfInsertAtSelection, hr=0x%08X", hr);
        return false;
    }

    // Query insertion point (TF_IAS_QUERYONLY)
    ITfRange* pRange = nullptr;
    hr = pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &pRange);
    pInsertAtSelection->Release();

    if (FAILED(hr) || pRange == nullptr) {
        pContextComposition->Release();
        TSF_LOG(L"StartComposition: Failed InsertTextAtSelection, hr=0x%08X", hr);
        return false;
    }

    // Start composition at this range (pass this as sink to receive termination events)
    ITfComposition* pComposition = nullptr;
    hr = pContextComposition->StartComposition(ec, pRange, this, &pComposition);
    pContextComposition->Release();

    if (FAILED(hr) || pComposition == nullptr) {
        pRange->Release();
        TSF_LOG(L"StartComposition: StartComposition FAILED, hr=0x%08X", hr);
        return false;
    }

    // StartComposition returned ONE ref in `pComposition`. We must hold our own ref
    // in pComposition_ (it outlives this call) AND the caller also needs a ref for
    // their own use. AddRef once more so caller owning a CComPtr/raw ref can safely
    // Release without freeing the composition while we still hold pComposition_.
    pComposition_ = pComposition;      // takes the one ref from StartComposition
    pComposition_->AddRef();           // now 2 refs total — one for us, one for caller
    pContext_ = pContext;
    pContext_->AddRef();

    // Set selection to the insertion range (VietType pattern - helps apps properly track insertion point)
    TF_SELECTION sel;
    sel.range = pRange;
    sel.style.ase = TF_AE_NONE;
    sel.style.fInterimChar = FALSE;
    pContext->SetSelection(ec, 1, &sel);

    pRange->Release();
    *ppComposition = pComposition;     // caller owns 1 ref, must Release (or CComPtr does it)

    TSF_LOG(L"StartComposition: SUCCESS");
    return true;
}

bool CompositionManager::SetCompositionText(TfEditCookie ec, const std::wstring& text) {
    if (pComposition_ == nullptr) {
        TSF_LOG(L"SetCompositionText: ERROR - No active composition");
        return false;
    }

    // Get the composition's range (this is the authoritative range)
    ITfRange* pRange = nullptr;
    HRESULT hr = pComposition_->GetRange(&pRange);
    if (FAILED(hr) || pRange == nullptr) {
        TSF_LOG(L"SetCompositionText: Failed to get range, hr=0x%08X", hr);
        return false;
    }

    // Set text in the composition range (use TF_ST_CORRECTION for proper text handling)
    // This flag helps prevent the composition from overwriting adjacent text
    hr = pRange->SetText(ec, TF_ST_CORRECTION, text.c_str(), static_cast<LONG>(text.length()));

    if (FAILED(hr)) {
        pRange->Release();
        TSF_LOG(L"SetCompositionText: SetText FAILED, hr=0x%08X", hr);
        return false;
    }

    // Skip display attribute - some apps (like Notepad++ with Scintilla) don't handle it well
    // and it can cause visual overlay issues
    // ApplyDisplayAttribute(ec, pRange);

    // Sync cursor
    MoveCaretToEnd(ec);

    pRange->Release();

    TSF_LOG(L"SetCompositionText: text='%ls'", text.c_str());
    return true;
}

void CompositionManager::EndComposition(TfEditCookie ec) {
    if (pComposition_ == nullptr) return;

    TSF_LOG(L"EndComposition: Finalizing and releasing to app");

    ITfRange* pRange = nullptr;
    if (SUCCEEDED(pComposition_->GetRange(&pRange)) && pRange) {
        // Clear display attributes BEFORE ending
        ClearDisplayAttribute(ec, pRange);
        pRange->Release();
    }

    // Note: Don't call MoveCaretToEnd here - it's already called in SetCompositionText
    // Calling it again can interfere with cursor positioning in some apps (like Notepad++)

    // Inform TSF that the composition is finished
    pComposition_->EndComposition(ec);
    pComposition_->Release();
    pComposition_ = nullptr;
    pContext_->Release();
    pContext_ = nullptr;

    TSF_LOG(L"EndComposition: COMPLETED");
}

void CompositionManager::TerminateComposition() {
    // This is only a safety fallback. Normal termination should use EndComposition(ec).
    if (pComposition_) {
        TSF_LOG(L"TerminateComposition: Warning - local state cleared without EndComposition call");
        pComposition_->Release();
        pComposition_ = nullptr;
    }
    if (pContext_) {
        pContext_->Release();
        pContext_ = nullptr;
    }
}

void CompositionManager::ApplyDisplayAttribute(TfEditCookie ec, ITfRange* pRange) {
    if (pRange == nullptr || pContext_ == nullptr) return;

    // Get display attribute property
    ITfProperty* pProperty = nullptr;
    if (FAILED(pContext_->GetProperty(GUID_PROP_ATTRIBUTE, &pProperty)) || pProperty == nullptr) {
        return;
    }

    // Register GUID with CategoryMgr each time (VietType pattern)
    if (pCategoryMgr_ != nullptr) {
        TfGuidAtom atom = TF_INVALID_GUIDATOM;
        HRESULT hr = pCategoryMgr_->RegisterGUID(GUID_DisplayAttribute_Input, &atom);
        if (SUCCEEDED(hr) && atom != TF_INVALID_GUIDATOM) {
            // Set the display attribute on the range
            VARIANT var;
            var.vt = VT_I4;
            var.lVal = atom;
            pProperty->SetValue(ec, pRange, &var);
            TSF_LOG(L"Applied display attribute");
        }
    }

    pProperty->Release();
}

void CompositionManager::ClearDisplayAttribute(TfEditCookie ec, ITfRange* pRange) {
    if (pRange == nullptr || pContext_ == nullptr) return;

    ITfProperty* pProperty = nullptr;
    if (SUCCEEDED(pContext_->GetProperty(GUID_PROP_ATTRIBUTE, &pProperty)) && pProperty != nullptr) {
        pProperty->Clear(ec, pRange);
        pProperty->Release();
        TSF_LOG(L"Cleared display attribute");
    }
}

void CompositionManager::MoveCaretToEnd(TfEditCookie ec) {
    if (pComposition_ == nullptr || pContext_ == nullptr) return;

    ITfRange* pRange = nullptr;
    HRESULT hr = pComposition_->GetRange(&pRange);
    if (FAILED(hr) || pRange == nullptr) return;

    // Clone and collapse to end
    ITfRange* pRangeClone = nullptr;
    hr = pRange->Clone(&pRangeClone);
    if (SUCCEEDED(hr) && pRangeClone != nullptr) {
        pRangeClone->Collapse(ec, TF_ANCHOR_END);

        // Set selection to collapsed range
        TF_SELECTION sel;
        sel.range = pRangeClone;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        pContext_->SetSelection(ec, 1, &sel);

        pRangeClone->Release();
    }
    pRange->Release();
}

}  // namespace TSF
}  // namespace NextKey
