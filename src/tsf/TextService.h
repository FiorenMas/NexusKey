// VKey - Text Service Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "stdafx.h"
#include <memory>

namespace NextKey {
namespace TSF {

// Forward declarations
class KeyEventSink;
class EngineController;
class ReadonlyContextProvider;

// DLL reference management
void DllAddRef();
void DllRelease();

/// Text Service - main TSF Text Input Processor
/// Implements ITfDisplayAttributeProvider for invisible composition (no underline/highlight)
class TextService : public ITfTextInputProcessor,
                    public ITfDisplayAttributeProvider {
public:
    TextService();
    virtual ~TextService();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    IFACEMETHODIMP Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) override;
    IFACEMETHODIMP Deactivate() override;

    // ITfDisplayAttributeProvider
    IFACEMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    IFACEMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

    // Accessors
    TfClientId GetClientId() const { return clientId_; }
    ITfThreadMgr* GetThreadMgr() const { return pThreadMgr_; }

private:
    ULONG refCount_ = 1;
    ITfThreadMgr* pThreadMgr_ = nullptr;
    ITfCategoryMgr* pCategoryMgr_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;

    std::unique_ptr<KeyEventSink> keyEventSink_;
    std::unique_ptr<EngineController> engineController_;
    std::unique_ptr<ReadonlyContextProvider> readonlyProvider_;
};

/// Text Service Class Factory
class TextServiceFactory : public IClassFactory {
public:
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override { return 1; }
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP LockServer(BOOL fLock) override;
};

}  // namespace TSF
}  // namespace NextKey
