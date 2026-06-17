// VKey - TSF DLL Entry Point
// SPDX-License-Identifier: AGPL-3.0-only

#include "stdafx.h"
#include "Globals.h"
#include "TextService.h"
#include "ComUtils.h"
#include "core/Logger.h"

namespace NextKey {
namespace TSF {

// Global class factory
static TextServiceFactory g_classFactory;

}  // namespace TSF
}  // namespace NextKey

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/) {
    switch (dwReason) {
        case DLL_PROCESS_ATTACH: {
            NextKey::TSF::g_hInstance = hInstance;
            DisableThreadLibraryCalls(hInstance);
            // Tell the Logger where the DLL lives so the file lands in the
            // install dir (next to VKeyApp.exe) rather than the host
            // process's directory (chrome.exe etc., which is usually not
            // writable). Logger falls back to %APPDATA%\VKey\logs if
            // install dir is read-only.
            wchar_t dllPath[MAX_PATH] = {0};
            DWORD n = GetModuleFileNameW(hInstance, dllPath, MAX_PATH);
            if (n > 0) {
                std::wstring path(dllPath);
                size_t slash = path.find_last_of(L"\\/");
                if (slash != std::wstring::npos) {
                    ::NextKey::Logger::SetInstallDir(path.substr(0, slash));
                }
            }
            // Brand the log file with the host process so each DLL instance
            // (chrome.exe renderer, Electron helper, …) is identifiable at a
            // glance. PID suffix is appended by Logger because RoleTag starts
            // with "TSF-".
            {
                wchar_t hostPath[MAX_PATH] = {0};
                if (GetModuleFileNameW(nullptr, hostPath, MAX_PATH) > 0) {
                    std::wstring p(hostPath);
                    size_t s = p.find_last_of(L"\\/");
                    std::wstring base = (s == std::wstring::npos) ? p : p.substr(s + 1);
                    size_t dot = base.find_last_of(L'.');
                    if (dot != std::wstring::npos) base.resize(dot);
                    ::NextKey::Logger::SetRoleTag(L"TSF-" + base);
                }
            }
            break;
        }

        case DLL_PROCESS_DETACH:
            ::NextKey::Logger::Shutdown();
            break;
    }
    return TRUE;
}

extern "C" {

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (!IsEqualCLSID(rclsid, NextKey::TSF::CLSID_TextService)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    return NextKey::TSF::g_classFactory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() {
    return (NextKey::TSF::g_dllRefCount == 0) ? S_OK : S_FALSE;
}

}  // extern "C"
