// VKey - Win32 Dark Mode Helper Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "DarkModeHelper.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

// Undocumented uxtheme.dll APIs for dark mode support
enum class PreferredAppMode { Default = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3 };
using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
using fnAllowDarkModeForWindow = bool(WINAPI*)(HWND, bool);
using fnRefreshImmersiveColorPolicyState = void(WINAPI*)();

namespace NextKey {
namespace DarkModeHelper {

bool IsWindowsDarkMode() noexcept {
    HKEY hKey;
    DWORD value = 1;  // Default: light mode (safe fallback)
    DWORD size = sizeof(value);

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);
    }

    return value == 0;
}

bool IsTaskbarDark() noexcept {
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    bool found = false;

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        // SystemUsesLightTheme was added in Win10 1903. On older builds the value
        // is absent — fall back to the app theme so behavior stays sensible.
        // Also require REG_DWORD: a tampered/corrupt value of another type would
        // otherwise feed garbage bytes into `value`.
        if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", nullptr, &type,
                             reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS
            && type == REG_DWORD && size == sizeof(value)) {
            found = true;
        }
        RegCloseKey(hKey);
    }

    return found ? (value == 0) : IsWindowsDarkMode();
}

bool IsWindows11OrGreater() noexcept {
    // Cache result — OS version doesn't change at runtime, and this is called
    // frequently (e.g., per WM_NCHITTEST in handleWindowDrag).
    static const bool cached = [] {
        using RtlGetVersionPtr = LONG(WINAPI*)(OSVERSIONINFOW*);

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll) return false;

        auto RtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hNtdll, "RtlGetVersion"));
        if (!RtlGetVersion) return false;

        OSVERSIONINFOW osInfo = { 0 };
        osInfo.dwOSVersionInfoSize = sizeof(osInfo);

        if (RtlGetVersion(&osInfo) != 0) return false;  // STATUS_SUCCESS = 0

        // Windows 11 is Windows NT 10.0 with build >= 22000
        return (osInfo.dwMajorVersion > 10) ||
               (osInfo.dwMajorVersion == 10 && osInfo.dwBuildNumber >= 22000);
    }();
    return cached;
}

void ApplyDarkModeForApp() noexcept {
    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (!hUxTheme) return;

    // Ordinal 135: SetPreferredAppMode (Windows 1903+)
    auto setMode = reinterpret_cast<fnSetPreferredAppMode>(
        GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));

    // Ordinal 104: RefreshImmersiveColorPolicyState
    auto refresh = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(
        GetProcAddress(hUxTheme, MAKEINTRESOURCEA(104)));

    if (setMode) {
        setMode(PreferredAppMode::AllowDark);
    }
    if (refresh) {
        refresh();
    }
}

void SetWindowDarkMode(HWND hwnd, bool dark) noexcept {
    if (!hwnd) return;

    // DWM dark title bar
    BOOL darkMode = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkMode, sizeof(darkMode));

    // Let Windows 11 natively handle the DWM border. Its default border is highly optimized
    // for rounded windows and avoids the thick corner artifacts caused by custom opaque colors.

    // uxtheme per-window dark mode (for context menus, scrollbars)
    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxTheme) {
        auto allowDark = reinterpret_cast<fnAllowDarkModeForWindow>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
        if (allowDark) {
            allowDark(hwnd, dark);
        }
    }
}

}  // namespace DarkModeHelper
}  // namespace NextKey
