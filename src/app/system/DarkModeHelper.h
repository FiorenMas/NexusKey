// VKey - Win32 Dark Mode Helper
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pure Win32 utilities for dark mode detection and application.
// No Sciter dependency — safe for LITE_MODE builds.

#pragma once
#include <windows.h>
#include <string>

namespace NextKey {
namespace DarkModeHelper {

    /**
     * Detect Windows app theme via registry (AppsUseLightTheme).
     * @return true if dark mode is active, false for light mode
     */
    [[nodiscard]] bool IsWindowsDarkMode() noexcept;

    /**
     * Detect Windows taskbar/system theme via registry (SystemUsesLightTheme).
     * Differs from IsWindowsDarkMode() when the user has split app/system themes.
     * Use this for tray icons and anything else rendered on the taskbar surface.
     * Falls back to the app theme if the key is missing (older builds).
     * @return true if the taskbar is dark, false for light
     */
    [[nodiscard]] bool IsTaskbarDark() noexcept;

    /**
     * Detect Windows 11 or greater (build >= 22000).
     * @return true if Windows 11+, false for Windows 10 or older
     */
    [[nodiscard]] bool IsWindows11OrGreater() noexcept;

    /**
     * Apply dark mode for the entire app process using undocumented uxtheme APIs.
     * Call once at startup. Enables dark context menus, scrollbars, etc.
     */
    void ApplyDarkModeForApp() noexcept;

    /**
     * Set dark mode for a specific window (DWM title bar + uxtheme controls).
     * @param hwnd Window handle
     * @param dark true for dark mode, false for light mode
     */
    void SetWindowDarkMode(HWND hwnd, bool dark) noexcept;

    /**
     * Build CSS body classes for Sciter dialogs ("dark", "win10", or both).
     * @param dark true if dark mode is active
     * @return Space-separated class string (e.g. "dark win10")
     */
    [[nodiscard]] inline std::wstring BuildBodyClasses(bool dark) {
        std::wstring classes = dark ? L"dark" : L"";
        if (!IsWindows11OrGreater()) {
            if (!classes.empty()) classes += L" ";
            classes += L"win10";
        }
        return classes;
    }

}  // namespace DarkModeHelper
}  // namespace NextKey
