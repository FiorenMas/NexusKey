// VKey - Input Scope Checker for TSF
// SPDX-License-Identifier: AGPL-3.0-only
//
// Checks if a TSF context should block Vietnamese input:
// - GUID_COMPARTMENT_KEYBOARD_DISABLED compartment
// - Input scopes: password, PIN, email, login fields

#pragma once

#include "EditSession.h"
#include "Define.h"
#include <InputScope.h>

// GUID_PROP_INPUTSCOPE is declared in InputScope.h but not defined in uuid.lib.
// Use an inline const GUID to avoid DEFINE_GUID/INITGUID linker issues (C++17).
// {1713DD5A-68E7-4A5B-9AF6-592A595C778D}
inline const GUID kGuidPropInputScope =
    { 0x1713DD5A, 0x68E7, 0x4A5B, { 0x9A, 0xF6, 0x59, 0x2A, 0x59, 0x5C, 0x77, 0x8D } };

namespace NextKey {
namespace TSF {

/// Edit session that checks if the current context blocks keyboard input.
/// Result is written to the bool* passed at construction.
class InputScopeCheckSession : public EditSession {
public:
    InputScopeCheckSession(ITfContext* pContext, bool* pBlocked)
        : EditSession(pContext), pBlocked_(pBlocked) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (!pContext_ || !pBlocked_) return E_FAIL;
        *pBlocked_ = false;

        // 1. Check GUID_COMPARTMENT_KEYBOARD_DISABLED
        ITfCompartmentMgr* pCompMgr = nullptr;
        HRESULT hr = pContext_->QueryInterface(IID_ITfCompartmentMgr, (void**)&pCompMgr);
        if (SUCCEEDED(hr) && pCompMgr) {
            ITfCompartment* pComp = nullptr;
            hr = pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &pComp);
            if (SUCCEEDED(hr) && pComp) {
                VARIANT var;
                VariantInit(&var);
                hr = pComp->GetValue(&var);
                if (SUCCEEDED(hr) && var.vt == VT_I4 && var.lVal != 0) {
                    TSF_LOG(L"InputScopeCheck: KEYBOARD_DISABLED compartment set");
                    *pBlocked_ = true;
                }
                VariantClear(&var);
                pComp->Release();
            }
            pCompMgr->Release();
        }

        if (*pBlocked_) return S_OK;

        // 2. Check input scopes (password, PIN, email, etc.)
        ITfReadOnlyProperty* pProp = nullptr;
        hr = pContext_->GetAppProperty(kGuidPropInputScope, &pProp);
        if (FAILED(hr) || !pProp) return S_OK;

        // Get selection to query input scope at cursor position
        TF_SELECTION sel = {};
        ULONG fetched = 0;
        hr = pContext_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
        if (FAILED(hr) || fetched == 0) {
            pProp->Release();
            return S_OK;
        }

        VARIANT var;
        VariantInit(&var);
        hr = pProp->GetValue(ec, sel.range, &var);
        sel.range->Release();
        pProp->Release();

        if (FAILED(hr) || var.vt != VT_UNKNOWN || !var.punkVal) {
            VariantClear(&var);
            return S_OK;
        }

        ITfInputScope* pInputScope = nullptr;
        hr = var.punkVal->QueryInterface(IID_ITfInputScope, (void**)&pInputScope);
        VariantClear(&var);

        if (FAILED(hr) || !pInputScope) return S_OK;

        InputScope* pScopes = nullptr;
        UINT scopeCount = 0;
        hr = pInputScope->GetInputScopes(&pScopes, &scopeCount);
        pInputScope->Release();

        if (FAILED(hr) || !pScopes) return S_OK;

        for (UINT i = 0; i < scopeCount; ++i) {
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
                    TSF_LOG(L"InputScopeCheck: blocked scope %d", pScopes[i]);
                    *pBlocked_ = true;
                    break;
                default:
                    break;
            }
            if (*pBlocked_) break;
        }

        CoTaskMemFree(pScopes);
        return S_OK;
    }

private:
    bool* pBlocked_;
};

}  // namespace TSF
}  // namespace NextKey
