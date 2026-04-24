// NexusKey - System Configuration
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <climits>
#include <cstdint>

namespace NextKey {

/// Icon style for tray/language bar
enum class IconStyle : uint8_t {
    Color = 0,   // Default colored icons (V=pink, E=blue)
    Dark = 1,    // White icons (for dark taskbar)
    Light = 2,   // Black icons (for light taskbar)
    Custom = 3,  // User-selected custom colors
    Auto = 4     // Follow Windows taskbar theme (SystemUsesLightTheme)
};

/// Default icon colors (COLORREF format: 0x00BBGGRR)
/// V: RGB(243, 98, 103) = #F36267
/// E: RGB(47, 175, 218)  = #2FAFDA
inline constexpr uint32_t DEFAULT_ICON_COLOR_V = 0x006762F3;  // BGR
inline constexpr uint32_t DEFAULT_ICON_COLOR_E = 0x00DAAF2F;  // BGR

/// System-level configuration (EXE-only, not shared with DLL)
/// Stored in [system] section of config.toml
struct SystemConfig {
    bool runAtStartup = false;     // Register to run on Windows logon
    bool runAsAdmin = false;       // Run with elevated privileges (Task Scheduler)
    bool showOnStartup = true;     // Open settings dialog on app startup
    bool desktopShortcut = false;  // Desktop shortcut exists

    // Theme override
    bool forceLightTheme = false;  // Always use light theme (ignore Windows dark mode)

    uint8_t language = 0;


    // Icon customization
    uint8_t iconStyle = 0;         // IconStyle enum (0=Color, 1=Dark, 2=Light, 3=Custom, 4=Auto)
    uint32_t customColorV = 0;     // Custom V color (COLORREF, 0 = use default)
    uint32_t customColorE = 0;     // Custom E color (COLORREF, 0 = use default)

    // Floating V/E icon overlay (draggable)
    bool showFloatingIcon = false;     // Show floating V/E indicator
    int32_t floatingIconX = INT32_MIN; // Saved X position (INT32_MIN = default)
    int32_t floatingIconY = INT32_MIN; // Saved Y position (INT32_MIN = default)

    // Startup mode
    uint8_t startupMode = 0;   // 0=Vietnamese, 1=English, 2=Remember (Smart Switch persist)

    // Auto-update
    bool autoCheckUpdate = true;   // Check for updates on startup

    /// Get effective V color (default if custom not set)
    [[nodiscard]] uint32_t GetEffectiveColorV() const noexcept {
        return customColorV != 0 ? customColorV : DEFAULT_ICON_COLOR_V;
    }

    /// Get effective E color (default if custom not set)
    [[nodiscard]] uint32_t GetEffectiveColorE() const noexcept {
        return customColorE != 0 ? customColorE : DEFAULT_ICON_COLOR_E;
    }

    /// Check if UI language is English (derived from language field)
    [[nodiscard]] bool IsEnglishUI() const noexcept { return language == 1; }

    SystemConfig() = default;
};

}  // namespace NextKey
