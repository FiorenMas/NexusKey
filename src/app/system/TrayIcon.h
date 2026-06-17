// VKey - System Tray Icon
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "core/ipc/SharedConstants.h"
#include "core/ipc/SharedStateManager.h"
#include "core/config/TypingConfig.h"
#include "core/SystemConfig.h"
#include <Windows.h>
#include <shellapi.h>
#include <functional>
#include <utility>
#include <string>

namespace NextKey {

/// Menu item identifiers
enum class TrayMenuId : UINT {
    Settings = 1001,
    About = 1002,
    Exit = 1003,
    ToggleMode = 1004,
    // Code table submenu (1010-1014 = CodeTable enum values + offset)
    CodeTableUnicode = 1010,
    CodeTableTCVN3 = 1011,
    CodeTableVNI = 1012,
    CodeTableCompound = 1013,
    CodeTableCP1258 = 1014,
    // Feature toggles
    SpellCheck = 1020,
    SmartSwitch = 1021,
    MacroEnabled = 1022,
    // Tools
    MacroTable = 1030,
    ConvertTool = 1031,
    QuickConvert = 1032,
    // Input method submenu — IDs must stay contiguous; callback derives
    // InputMethod via (id - InputTelex). Add new methods at the end.
    InputTelex = 1040,
    InputVNI = 1041,
    InputSimpleTelex = 1042,
    InputCombined = 1043,
    InputUserDefined = 1044,
    // Hybrid TSF update — restart prompt (only shown when any update flag is live)
    RestartWindows = 1050,
    // Watchdog control (toggle — label switches based on TrayMenuState::watchdogEnabled)
    ToggleWatchdog = 1060,
};

/// Callback type for tray events
using MenuCallback = std::function<void(TrayMenuId)>;

/// Callback for settings dialog requesting a specific V/E mode
using ModeRequestCallback = std::function<void(bool vietnamese)>;

/// Snapshot of current state for right-click menu checkmarks
struct TrayMenuState {
    bool vietnamese = true;
    bool spellCheck = false;
    bool smartSwitch = false;
    bool macroEnabled = false;
    int inputMethod = 0;       // 0=Telex, 1=VNI, 2=SimpleTelex, 3=Combined, 4=UserDefined
    CodeTable codeTable = CodeTable::Unicode;
    bool watchdogEnabled = false;  // Auto-restart on crash (opt-in)
};

/// Callback to query current menu state (pull model — called when menu opens)
using MenuStateGetter = std::function<TrayMenuState()>;

/// System tray icon manager — always visible, shows V/E state
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    /// Initialize and add tray icon
    [[nodiscard]] bool Create(HINSTANCE hInstance, bool initialVietnamese = true);

    /// Destroy tray icon
    void Destroy() noexcept;

    /// Update icon to reflect Vietnamese/English mode
    void SetVietnameseMode(bool enabled) noexcept;

    /// Set icon style and custom colors (triggers icon refresh)
    void SetIconConfig(uint8_t style, uint32_t colorV, uint32_t colorE) noexcept;

    /// Set callback for menu/click actions
    void SetMenuCallback(MenuCallback callback) noexcept { menuCallback_ = std::move(callback); }

    /// Set callback for settings dialog mode requests (cross-process)
    void SetModeRequestCallback(ModeRequestCallback callback) noexcept { modeRequestCallback_ = std::move(callback); }

    /// Set getter to query current state when right-click menu opens
    void SetMenuStateGetter(MenuStateGetter getter) noexcept { menuStateGetter_ = std::move(getter); }

    /// Set callback when system config changes (WM_VKEY_ICON_CHANGED)
    void SetIconConfigChangedCallback(std::function<void()> callback) noexcept { iconConfigChangedCallback_ = std::move(callback); }

    /// Set callback when hook config changes (WM_VKEY_HOOK_RELOAD) — subprocess → main eager sync
    void SetHookReloadCallback(std::function<void()> callback) noexcept { hookReloadCallback_ = std::move(callback); }

    /// Non-owning pointer to the process-wide SharedStateManager. Used only to
    /// read TSF-update flags for the restart-menu item. Safe to pass &g_sharedState.
    void SetSharedState(SharedStateManager* mgr) noexcept { sharedState_ = mgr; }

    /// Process window messages (call from WndProc)
    [[nodiscard]] bool ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    /// Get the hidden message window handle (for timers/hotkeys)
    [[nodiscard]] HWND GetMessageWindow() const noexcept { return hwndMessage_; }

    [[nodiscard]] bool IsVietnameseMode() const noexcept { return vietnameseMode_; }

    /// Rebuild cached convert-hotkey text from TOML. Call after SaveConvertConfig.
    void RefreshConvertHotkeyCache();

    /// Same but reuses an already-loaded ConvertConfig — avoids a redundant TOML read
    /// when the caller has just loaded it for another purpose.
    void RefreshConvertHotkeyCache(const ConvertConfig& cc);

private:
    void ShowContextMenu();
    void RefreshIcon() noexcept;  // Reload icon based on current style/mode
    void ReAddIcon() noexcept;    // Re-register tray icon (after explorer restart or NIM_MODIFY failure)
    [[nodiscard]] HICON CreateColorizedIcon(int baseIconId, COLORREF color) noexcept;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwndMessage_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    bool vietnameseMode_ = true;
    bool toggledByClick_ = false;        // Single-click toggled — undo if double-click follows
    bool ignoreNextLButtonUp_ = false;   // Suppress WM_LBUTTONUP after WM_LBUTTONDBLCLK
    MenuCallback menuCallback_;
    ModeRequestCallback modeRequestCallback_;
    MenuStateGetter menuStateGetter_;
    std::function<void()> iconConfigChangedCallback_;
    std::function<void()> hookReloadCallback_;
    SharedStateManager* sharedState_ = nullptr;  // non-owning; for TSF-update flag checks

    // Icon style configuration
    uint8_t iconStyle_ = 0;              // 0=Color, 1=Dark/White, 2=Light/Black, 3=Custom
    uint32_t customColorV_ = 0;          // Custom V color (COLORREF, 0=default)
    uint32_t customColorE_ = 0;          // Custom E color (COLORREF, 0=default)
    HICON customIcon_ = nullptr;          // Cached custom-colorized icon (needs DestroyIcon)

    // Cached hotkey text for Quick Convert menu item (updated on config change)
    std::wstring cachedConvertHotkeyText_;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    UINT wmTaskbarCreated_ = 0;           // Registered "TaskbarCreated" message ID
};

}  // namespace NextKey
