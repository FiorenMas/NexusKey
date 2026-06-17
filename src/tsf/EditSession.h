// VKey - Edit Session Helper
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <msctf.h>

namespace NextKey {
namespace TSF {

// Edit session base class for TSF text operations
// Derived classes implement DoEditSession() to perform text edits
class EditSession : public ITfEditSession {
public:
    EditSession(ITfContext* pContext) : refCount_(1), pContext_(pContext) {
        if (pContext_) pContext_->AddRef();
    }

    virtual ~EditSession() {
        if (pContext_) pContext_->Release();
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (ppvObj == nullptr) return E_INVALIDARG;
        *ppvObj = nullptr;

        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppvObj = static_cast<ITfEditSession*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&refCount_);
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }

    // ITfEditSession - must be implemented by derived classes
    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override = 0;

protected:
    ITfContext* pContext_;

private:
    ULONG refCount_;
};

}  // namespace TSF
}  // namespace NextKey
