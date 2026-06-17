// VKey - TSF Language Bar Button Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "stdafx.h"
#include "LanguageBarButton.h"
#include "EngineController.h"
#include "Globals.h"
#include "Define.h"
#include "ComUtils.h"
#include "resource.h"
#include "core/Strings.h"
#include <strsafe.h>
#include <shellapi.h>

namespace NextKey {
namespace TSF {

// Menu item IDs for right-click context menu
static constexpr UINT MENU_ID_SETTINGS = 1;
static constexpr UINT MENU_ID_ABOUT = 2;

// Code table submenu IDs (10-14 = CodeTable enum values + offset)
static constexpr UINT MENU_ID_CODETABLE_BASE = 10;
static constexpr UINT MENU_ID_CT_UNICODE  = 10;
static constexpr UINT MENU_ID_CT_TCVN3    = 11;
static constexpr UINT MENU_ID_CT_VNI      = 12;
static constexpr UINT MENU_ID_CT_COMPOUND = 13;
static constexpr UINT MENU_ID_CT_CP1258   = 14;

// Cookie for ITfSource sink identification
static constexpr DWORD LANGBAR_SINK_COOKIE = 0x4E4B4C42;  // 'NKLB'

LanguageBarButton::LanguageBarButton() {
    DllAddRef();
}

LanguageBarButton::~LanguageBarButton() {
    DllRelease();
}

// ============================================================================
// IUnknown
// ============================================================================

IFACEMETHODIMP LanguageBarButton::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton)) {
        *ppvObj = static_cast<ITfLangBarItemButton*>(this);
    } else if (IsEqualIID(riid, IID_ITfSource)) {
        *ppvObj = static_cast<ITfSource*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) LanguageBarButton::AddRef() {
    return InterlockedIncrement(&refCount_);
}

IFACEMETHODIMP_(ULONG) LanguageBarButton::Release() {
    ULONG count = InterlockedDecrement(&refCount_);
    if (count == 0) delete this;
    return count;
}

// ============================================================================
// ITfLangBarItem
// ============================================================================

IFACEMETHODIMP LanguageBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo) {
    if (pInfo == nullptr) return E_INVALIDARG;

    pInfo->clsidService = CLSID_TextService;
    pInfo->guidItem = GUID_LangBarItem_Toggle;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 0;
    StringCchCopyW(pInfo->szDescription, TF_LBI_DESC_MAXLEN, TEXT_SERVICE_DESCRIPTION);

    return S_OK;
}

IFACEMETHODIMP LanguageBarButton::GetStatus(DWORD* pdwStatus) {
    if (pdwStatus == nullptr) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

IFACEMETHODIMP LanguageBarButton::Show(BOOL /*fShow*/) {
    return E_NOTIMPL;
}

IFACEMETHODIMP LanguageBarButton::GetTooltipString(BSTR* pbstrToolTip) {
    if (pbstrToolTip == nullptr) return E_INVALIDARG;

    bool vietnamese = controller_ && controller_->IsVietnameseMode();
    *pbstrToolTip = SysAllocString(
        vietnamese ? S(StringId::TIP_VIETNAMESE) : S(StringId::TIP_ENGLISH));

    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

// ============================================================================
// ITfLangBarItemButton
// ============================================================================

IFACEMETHODIMP LanguageBarButton::OnClick(TfLBIClick click, POINT pt, const RECT* /*prcArea*/) {
    if (click == TF_LBI_CLK_LEFT) {
        // Toggle Vietnamese/English mode
        if (controller_) {
            controller_->ToggleVietnameseMode();
            TSF_LOG(L"LanguageBarButton: toggle V/E");
        }
        return S_OK;
    }

    if (click == TF_LBI_CLK_RIGHT) {
        // Show context menu
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return E_FAIL;

        // Code Table submenu
        if (controller_) {
            HMENU hCodeTableMenu = CreatePopupMenu();
            if (hCodeTableMenu) {
                CodeTable currentCT = controller_->GetCodeTable();
                auto addItem = [&](CodeTable ct, UINT id, const wchar_t* label) {
                    UINT flags = MF_STRING | (currentCT == ct ? MF_CHECKED : 0);
                    AppendMenuW(hCodeTableMenu, flags, id, label);
                };
                addItem(CodeTable::Unicode,         MENU_ID_CT_UNICODE,  L"Unicode");
                addItem(CodeTable::TCVN3,           MENU_ID_CT_TCVN3,    L"TCVN3 (ABC)");
                addItem(CodeTable::VNIWindows,      MENU_ID_CT_VNI,      L"VNI Windows");
                addItem(CodeTable::UnicodeCompound, MENU_ID_CT_COMPOUND, L"Unicode Compound");
                addItem(CodeTable::VietnameseLocale,MENU_ID_CT_CP1258,   L"CP 1258");

                AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hCodeTableMenu), L"Code Table");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            }
        }

        AppendMenuW(hMenu, MF_STRING, MENU_ID_SETTINGS, L"Settings...");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, MENU_ID_ABOUT, L"About VKey");

        UINT flags = TPM_NONOTIFY | TPM_RETURNCMD;
        if (GetSystemMetrics(SM_MENUDROPALIGNMENT)) {
            flags |= TPM_RIGHTALIGN;
        }

        HWND hwndOwner = GetFocus();
        if (!hwndOwner) hwndOwner = GetForegroundWindow();
        if (!hwndOwner) hwndOwner = GetDesktopWindow();
        UINT cmd = TrackPopupMenuEx(hMenu, flags, pt.x, pt.y, hwndOwner, nullptr);
        DestroyMenu(hMenu);

        if (cmd != 0) {
            (void)OnMenuSelect(cmd);
        }
        return S_OK;
    }

    return S_OK;
}

IFACEMETHODIMP LanguageBarButton::InitMenu(ITfMenu* pMenu) {
    if (pMenu == nullptr) return E_INVALIDARG;

    // Code Table submenu
    if (controller_) {
        ITfMenu* pSubMenu = nullptr;
        pMenu->AddMenuItem(0, TF_LBMENUF_SUBMENU, nullptr, nullptr, L"Code Table", 10, &pSubMenu);
        if (pSubMenu) {
            CodeTable currentCT = controller_->GetCodeTable();
            auto addItem = [&](CodeTable ct, UINT id, const wchar_t* label, ULONG len) {
                DWORD flags = (currentCT == ct) ? TF_LBMENUF_CHECKED : 0;
                pSubMenu->AddMenuItem(id, flags, nullptr, nullptr, label, len, nullptr);
            };
            addItem(CodeTable::Unicode,         MENU_ID_CT_UNICODE,  L"Unicode", 7);
            addItem(CodeTable::TCVN3,           MENU_ID_CT_TCVN3,    L"TCVN3 (ABC)", 11);
            addItem(CodeTable::VNIWindows,      MENU_ID_CT_VNI,      L"VNI Windows", 11);
            addItem(CodeTable::UnicodeCompound, MENU_ID_CT_COMPOUND, L"Unicode Compound", 17);
            addItem(CodeTable::VietnameseLocale,MENU_ID_CT_CP1258,   L"CP 1258", 7);
        }
        pMenu->AddMenuItem(0, TF_LBMENUF_SEPARATOR, nullptr, nullptr, L"", 0, nullptr);
    }

    pMenu->AddMenuItem(MENU_ID_SETTINGS, 0, nullptr, nullptr, L"Settings...", 12, nullptr);
    pMenu->AddMenuItem(0, TF_LBMENUF_SEPARATOR, nullptr, nullptr, L"", 0, nullptr);
    pMenu->AddMenuItem(MENU_ID_ABOUT, 0, nullptr, nullptr, L"About VKey", 15, nullptr);

    return S_OK;
}

IFACEMETHODIMP LanguageBarButton::OnMenuSelect(UINT wID) {
    // Code table menu items (10-14)
    if (wID >= MENU_ID_CT_UNICODE && wID <= MENU_ID_CT_CP1258) {
        if (controller_) {
            auto ct = static_cast<CodeTable>(wID - MENU_ID_CODETABLE_BASE);
            controller_->SetCodeTable(ct);
            TSF_LOG(L"LanguageBarButton: code table → %u", wID - MENU_ID_CODETABLE_BASE);
        }
        return S_OK;
    }

    switch (wID) {
        case MENU_ID_SETTINGS: {
            // Launch EXE with --settings flag
            wchar_t dllPath[MAX_PATH];
            GetModuleFileNameW(g_hInstance, dllPath, MAX_PATH);

            // Replace DLL filename with EXE filename (same directory)
            std::wstring exePath(dllPath);
            size_t pos = exePath.find_last_of(L"\\/");
            if (pos != std::wstring::npos) {
                exePath = exePath.substr(0, pos + 1);
            }
            exePath += L"VKey.exe";

            ShellExecuteW(nullptr, L"open", exePath.c_str(), L"--settings", nullptr, SW_SHOW);
            break;
        }

        case MENU_ID_ABOUT:
            MessageBoxW(nullptr,
                S(StringId::ABOUT_BODY),
                S(StringId::ABOUT_TITLE),
                MB_ICONINFORMATION);
            break;
    }
    return S_OK;
}

IFACEMETHODIMP LanguageBarButton::GetIcon(HICON* phIcon) {
    if (phIcon == nullptr) return E_INVALIDARG;
    if (!g_hInstance) return E_FAIL;

    bool vietnamese = controller_ && controller_->IsVietnameseMode();
    int iconId = vietnamese ? IDI_VIET_ON : IDI_VIET_OFF;

    // DPI-aware icon loading
    int iconx = GetSystemMetrics(SM_CXSMICON);
    int icony = GetSystemMetrics(SM_CYSMICON);

    *phIcon = static_cast<HICON>(
        LoadImageW(g_hInstance, MAKEINTRESOURCEW(iconId), IMAGE_ICON, iconx, icony, 0));

    return *phIcon ? S_OK : E_FAIL;
}

IFACEMETHODIMP LanguageBarButton::GetText(BSTR* pbstrText) {
    if (pbstrText == nullptr) return E_INVALIDARG;

    bool vietnamese = controller_ && controller_->IsVietnameseMode();
    *pbstrText = SysAllocString(vietnamese ? L"VIE" : L"NXK");

    return *pbstrText ? S_OK : E_OUTOFMEMORY;
}

// ============================================================================
// ITfSource
// ============================================================================

IFACEMETHODIMP LanguageBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) {
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) {
        return CONNECT_E_CANNOTCONNECT;
    }

    if (itemSink_) {
        return CONNECT_E_ADVISELIMIT;  // Only one sink at a time
    }

    HRESULT hr = punk->QueryInterface(IID_ITfLangBarItemSink, reinterpret_cast<void**>(&itemSink_));
    if (FAILED(hr)) {
        itemSink_ = nullptr;
        return E_NOINTERFACE;
    }

    *pdwCookie = LANGBAR_SINK_COOKIE;
    return S_OK;
}

IFACEMETHODIMP LanguageBarButton::UnadviseSink(DWORD dwCookie) {
    if (dwCookie != LANGBAR_SINK_COOKIE || !itemSink_) {
        return CONNECT_E_NOCONNECTION;
    }

    itemSink_->Release();
    itemSink_ = nullptr;
    return S_OK;
}

// ============================================================================
// Initialization / Lifecycle
// ============================================================================

bool LanguageBarButton::Initialize(EngineController* controller, ITfLangBarItemMgr* langBarItemMgr) {
    if (!controller || !langBarItemMgr) return false;

    controller_ = controller;
    langBarItemMgr_ = langBarItemMgr;
    langBarItemMgr_->AddRef();

    HRESULT hr = langBarItemMgr_->AddItem(this);
    if (FAILED(hr)) {
        TSF_LOG(L"LanguageBarButton: AddItem failed (hr=0x%08X)", hr);
        langBarItemMgr_->Release();
        langBarItemMgr_ = nullptr;
        return false;
    }

    TSF_LOG(L"LanguageBarButton initialized");
    return true;
}

void LanguageBarButton::Uninitialize() {
    if (langBarItemMgr_) {
        langBarItemMgr_->RemoveItem(this);
        langBarItemMgr_->Release();
        langBarItemMgr_ = nullptr;
    }

    if (itemSink_) {
        itemSink_->Release();
        itemSink_ = nullptr;
    }

    controller_ = nullptr;
    TSF_LOG(L"LanguageBarButton uninitialized");
}

void LanguageBarButton::Refresh() {
    if (itemSink_) {
        itemSink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
    }
}

}  // namespace TSF
}  // namespace NextKey
