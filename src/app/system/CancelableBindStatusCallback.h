// VKey - Cancelable IBindStatusCallback for URLDownloadToFileW
// SPDX-License-Identifier: AGPL-3.0-only
//
// Stack-allocated COM callback that aborts URLDownloadToFileW when a cancel
// flag is set.  Shared between UpdateChecker (ZIP download) and
// UpdateSecurity (SHA-256 sidecar download).
//
// Usage:
//   std::atomic<bool> cancel{false};
//   CancelableBindStatusCallback cb(cancel);
//   URLDownloadToFileW(nullptr, url, path, 0, &cb);
//
// Threading: the cancel flag is polled via relaxed load — cooperative
// cancellation, no ordering dependency.

#pragma once

#ifdef _WIN32

#include <atomic>
#include <urlmon.h>

namespace NextKey {

class CancelableBindStatusCallback : public IBindStatusCallback {
public:
    explicit CancelableBindStatusCallback(std::atomic<bool>& cancelFlag) noexcept
        : cancelFlag_(cancelFlag) {}

    // IUnknown — stack-allocated, no ref-counting.
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IUnknown || riid == IID_IBindStatusCallback) {
            *ppvObject = static_cast<IBindStatusCallback*>(this);
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // IBindStatusCallback
    STDMETHODIMP OnStartBinding(DWORD, IBinding*)               override { return S_OK; }
    STDMETHODIMP GetPriority(LONG*)                             override { return S_OK; }
    STDMETHODIMP OnLowResource(DWORD)                           override { return S_OK; }
    STDMETHODIMP OnProgress(ULONG, ULONG, ULONG, LPCWSTR)      override {
        if (cancelFlag_.load(std::memory_order_relaxed)) {
            return E_ABORT;
        }
        return S_OK;
    }
    STDMETHODIMP OnStopBinding(HRESULT, LPCWSTR)                override { return S_OK; }
    STDMETHODIMP GetBindInfo(DWORD*, BINDINFO*)                 override { return S_OK; }
    STDMETHODIMP OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    STDMETHODIMP OnObjectAvailable(REFIID, IUnknown*)           override { return S_OK; }

private:
    std::atomic<bool>& cancelFlag_;
};

}  // namespace NextKey

#endif  // _WIN32
