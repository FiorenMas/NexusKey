// VKey - Text Service Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "stdafx.h"
#include "TextService.h"
#include "Globals.h"
#include "KeyEventSink.h"
#include "EngineController.h"
#include "ReadonlyContextProvider.h"
#include "DisplayAttribute.h"
#include "ComUtils.h"
#include "Define.h"

namespace NextKey {
namespace TSF {

// ============================================================================
// TextService Implementation
// ============================================================================

TextService::TextService() {
    DllAddRef();
    TSF_LOG(L"TextService created");
}

TextService::~TextService() {
    TSF_LOG(L"TextService destroyed");
    DllRelease();
}

IFACEMETHODIMP TextService::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor)) {
        *ppvObj = static_cast<ITfTextInputProcessor*>(this);
    } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
        *ppvObj = static_cast<ITfDisplayAttributeProvider*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) TextService::AddRef() {
    return InterlockedIncrement(&refCount_);
}

IFACEMETHODIMP_(ULONG) TextService::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
        delete this;
    }
    return count;
}

IFACEMETHODIMP TextService::Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) {
    TSF_LOG(L"TextService::Activate");

    pThreadMgr_ = pThreadMgr;
    pThreadMgr_->AddRef();
    clientId_ = tfClientId;

    // Create category manager for display attributes
    HRESULT hrCat = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_ITfCategoryMgr, (void**)&pCategoryMgr_);
    if (FAILED(hrCat)) {
        TSF_LOG(L"CategoryMgr creation failed (hr=0x%08X) — no composition underline", hrCat);
    }

    // Initialize engine controller
    engineController_ = std::make_unique<EngineController>();
    engineController_->SetClientId(tfClientId);
    engineController_->SetCategoryMgr(pCategoryMgr_);

    // Initialize key event sink
    keyEventSink_ = std::make_unique<KeyEventSink>(this, engineController_.get());
    if (!keyEventSink_->Advise(pThreadMgr)) {
        TSF_LOG(L"Failed to advise KeyEventSink");
        keyEventSink_.reset();
        engineController_.reset();
        if (pCategoryMgr_) { pCategoryMgr_->Release(); pCategoryMgr_ = nullptr; }
        pThreadMgr_->Release(); pThreadMgr_ = nullptr;
        return E_FAIL;
    }

    // Initialize language bar button (V/E toggle icon in system tray)
    if (!engineController_->InitLanguageBar(pThreadMgr)) {
        TSF_LOG(L"Warning: Language bar button failed to initialize");
        // Non-fatal — typing still works without the icon
    }

    // Readonly context provider — publishes document anchor to SharedState so
    // HookEngine can do context-aware auto-cap without owning the TIP.
    // Gated on TSF_READONLY flag per-event; cheap when inactive.
    readonlyProvider_ = std::make_unique<ReadonlyContextProvider>(
        engineController_->GetSharedStateManager());
    if (!readonlyProvider_->Advise(pThreadMgr, tfClientId)) {
        TSF_LOG(L"Warning: ReadonlyContextProvider failed to advise — Hook fallback only");
        readonlyProvider_.reset();  // Non-fatal — continue without anchor publishing
    }

    TSF_LOG(L"TextService activated successfully");
    return S_OK;
}

IFACEMETHODIMP TextService::Deactivate() {
    TSF_LOG(L"TextService::Deactivate");

    // Clear TIP active flag before teardown — layout switched away from VKey
    if (engineController_) {
        engineController_->SetTsfTipActive(false);
    }

    if (readonlyProvider_) {
        readonlyProvider_->Unadvise();
        readonlyProvider_.reset();
    }

    if (engineController_) {
        engineController_->UninitLanguageBar();
    }

    if (keyEventSink_) {
        keyEventSink_->Unadvise();
        keyEventSink_.reset();
    }

    engineController_.reset();

    SafeRelease(pCategoryMgr_);
    SafeRelease(pThreadMgr_);
    clientId_ = TF_CLIENTID_NULL;

    TSF_LOG(L"TextService deactivated");
    return S_OK;
}

// ITfDisplayAttributeProvider - tells TSF to use our invisible display attribute
IFACEMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    // Simple implementation - apps will query by GUID instead
    if (ppEnum == nullptr) return E_INVALIDARG;
    *ppEnum = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (ppInfo == nullptr) return E_INVALIDARG;
    *ppInfo = nullptr;

    if (IsEqualGUID(guid, GUID_DisplayAttribute_Input)) {
        *ppInfo = new (std::nothrow) DisplayAttributeInfo();
        if (!*ppInfo) return E_OUTOFMEMORY;
        TSF_LOG(L"GetDisplayAttributeInfo: returned invisible attribute");
        return S_OK;
    }
    return E_INVALIDARG;
}

// ============================================================================
// TextServiceFactory Implementation
// ============================================================================

IFACEMETHODIMP TextServiceFactory::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppvObj = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

IFACEMETHODIMP TextServiceFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;

    TextService* pService = new (std::nothrow) TextService();
    if (pService == nullptr) return E_OUTOFMEMORY;

    HRESULT hr = pService->QueryInterface(riid, ppvObj);
    pService->Release();

    return hr;
}

IFACEMETHODIMP TextServiceFactory::LockServer(BOOL fLock) {
    if (fLock) {
        DllAddRef();
    } else {
        DllRelease();
    }
    return S_OK;
}

}  // namespace TSF
}  // namespace NextKey
