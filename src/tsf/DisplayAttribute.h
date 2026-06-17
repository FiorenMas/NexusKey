// VKey - Display Attribute (No Visual Styling)
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "stdafx.h"
#include "Globals.h"  // For GUID_DisplayAttribute_Input

namespace NextKey {
namespace TSF {

/// Display attribute with no visual styling (no underline, no highlight)
class DisplayAttributeInfo : public ITfDisplayAttributeInfo {
public:
    DisplayAttributeInfo() : refCount_(1) {}

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (ppvObj == nullptr) return E_INVALIDARG;
        *ppvObj = nullptr;

        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
            *ppvObj = static_cast<ITfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refCount_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }

    // ITfDisplayAttributeInfo
    IFACEMETHODIMP GetGUID(GUID* pguid) override {
        if (pguid == nullptr) return E_INVALIDARG;
        *pguid = GUID_DisplayAttribute_Input;
        return S_OK;
    }

    IFACEMETHODIMP GetDescription(BSTR* pbstrDesc) override {
        if (pbstrDesc == nullptr) return E_INVALIDARG;
        *pbstrDesc = SysAllocString(L"VKey Input");
        return (*pbstrDesc != nullptr) ? S_OK : E_OUTOFMEMORY;
    }

    IFACEMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override {
        if (pda == nullptr) return E_INVALIDARG;

        // Initialize all fields to zero first
        ZeroMemory(pda, sizeof(TF_DISPLAYATTRIBUTE));

        // No visual styling - invisible composition (matching VietType pattern)
        pda->crText.type = TF_CT_NONE;          // No text color change
        pda->crText.cr = 0;
        pda->crBk.type = TF_CT_NONE;            // No background color
        pda->crBk.cr = 0;
        pda->lsStyle = TF_LS_NONE;              // No underline
        pda->fBoldLine = FALSE;
        pda->crLine.type = TF_CT_NONE;          // No line color
        pda->crLine.cr = 0;
        pda->bAttr = TF_ATTR_INPUT;             // Mark as input

        return S_OK;
    }

    IFACEMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* /*pda*/) override {
        return S_OK;  // We don't allow modification
    }

    IFACEMETHODIMP Reset() override {
        return S_OK;  // Nothing to reset
    }

private:
    ULONG refCount_;
};


}  // namespace TSF
}  // namespace NextKey
