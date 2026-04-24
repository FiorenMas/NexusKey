// NexusKey - System Tray Icon Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "TrayIcon.h"
#include "../resource.h"
#include "UpdateChecker.h"
#include "ToastPopup.h"
#include "DarkModeHelper.h"
#include "StartupHelper.h"
#include "PendingDllApply.h"
#include "core/config/ConfigManager.h"
#include "core/Strings.h"
#include "core/CrashLog.h"
#include <strsafe.h>
#include <vector>
#include <CommCtrl.h>
#include <uxtheme.h>
#include <exception>
#include <thread>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace NextKey {

static TrayIcon* g_trayInstance = nullptr;

TrayIcon::TrayIcon() {
    g_trayInstance = this;
}

TrayIcon::~TrayIcon() {
    Destroy();
    if (customIcon_) {
        DestroyIcon(customIcon_);
        customIcon_ = nullptr;
    }
    g_trayInstance = nullptr;
}

bool TrayIcon::Create(HINSTANCE hInstance, bool initialVietnamese) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = L"NexusKeyTrayClass";

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    // Create as WS_OVERLAPPEDWINDOW (like OpenKey) so Task Manager recognizes
    // the window and picks up hIcon for the process icon, then hide it.
    hwndMessage_ = CreateWindowExW(
        0, L"NexusKeyTrayClass", L"NexusKey", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hwndMessage_) return false;

    // Enable dark mode for context menus (must be called before showing any menu)
    DarkModeHelper::ApplyDarkModeForApp();
    DarkModeHelper::SetWindowDarkMode(hwndMessage_, DarkModeHelper::IsWindowsDarkMode());

    ShowWindow(hwndMessage_, SW_HIDE);

    // Set window icon so Task Manager picks up the NK branding icon
    HICON appIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    if (appIcon) {
        SendMessageW(hwndMessage_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        SendMessageW(hwndMessage_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
    }

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwndMessage_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;

    // Load initial icon based on style
    vietnameseMode_ = initialVietnamese;
    RefreshIcon();
    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip),
                   S(initialVietnamese ? StringId::TIP_VIETNAMESE : StringId::TIP_ENGLISH));

    // Register "TaskbarCreated" message to detect explorer.exe restarts
    wmTaskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

    // Allow this message even when running with higher integrity (UIPI)
    if (wmTaskbarCreated_) {
        ChangeWindowMessageFilterEx(hwndMessage_, wmTaskbarCreated_, MSGFLT_ALLOW, nullptr);
    }

    // Always visible
    Shell_NotifyIconW(NIM_ADD, &nid_);
    RefreshConvertHotkeyCache();
    return true;
}

void TrayIcon::Destroy() noexcept {
    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
    }
    ZeroMemory(&nid_, sizeof(nid_));
    if (hwndMessage_) {
        DestroyWindow(hwndMessage_);
        hwndMessage_ = nullptr;
    }
}

void TrayIcon::SetVietnameseMode(bool enabled) noexcept {
    if (vietnameseMode_ == enabled) return;
    vietnameseMode_ = enabled;

    RefreshIcon();

    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip),
        enabled ? S(StringId::TIP_VIETNAMESE) : S(StringId::TIP_ENGLISH));

    if (nid_.hWnd) {
        if (!Shell_NotifyIconW(NIM_MODIFY, &nid_)) {
            // Icon may have been lost (explorer restart, GDI quota, etc.)
            // Re-add to recover
            Shell_NotifyIconW(NIM_ADD, &nid_);
        }
    }
}

void TrayIcon::SetIconConfig(uint8_t style, uint32_t colorV, uint32_t colorE) noexcept {
    if (iconStyle_ == style && customColorV_ == colorV && customColorE_ == colorE) return;

    iconStyle_ = style;
    customColorV_ = colorV;
    customColorE_ = colorE;

    RefreshIcon();

    if (nid_.hWnd) {
        if (!Shell_NotifyIconW(NIM_MODIFY, &nid_)) {
            Shell_NotifyIconW(NIM_ADD, &nid_);
        }
    }
}

void TrayIcon::ReAddIcon() noexcept {
    if (!nid_.hWnd) return;
    // Force re-register: delete first (may fail if already gone), then add
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    Shell_NotifyIconW(NIM_ADD, &nid_);
}

void TrayIcon::RefreshIcon() noexcept {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // Free previous custom icon if any
    if (customIcon_) {
        DestroyIcon(customIcon_);
        customIcon_ = nullptr;
    }

    HICON newIcon = nullptr;

    switch (static_cast<IconStyle>(iconStyle_)) {
        case IconStyle::Dark:
            newIcon = LoadIconW(hInstance,
                MAKEINTRESOURCEW(vietnameseMode_ ? IDI_VIET_ON_WHITE : IDI_VIET_OFF_WHITE));
            break;

        case IconStyle::Light:
            newIcon = LoadIconW(hInstance,
                MAKEINTRESOURCEW(vietnameseMode_ ? IDI_VIET_ON_BLACK : IDI_VIET_OFF_BLACK));
            break;

        case IconStyle::Custom: {
            const int baseIcon = vietnameseMode_ ? IDI_VIET_ON : IDI_VIET_OFF;
            const COLORREF color = static_cast<COLORREF>(
                vietnameseMode_
                    ? (customColorV_ != 0 ? customColorV_ : DEFAULT_ICON_COLOR_V)
                    : (customColorE_ != 0 ? customColorE_ : DEFAULT_ICON_COLOR_E));
            newIcon = CreateColorizedIcon(baseIcon, color);
            if (newIcon) {
                customIcon_ = newIcon;  // Track for cleanup
            }
            break;
        }

        case IconStyle::Auto: {
            const bool taskbarDark = DarkModeHelper::IsTaskbarDark();
            const int iconId = taskbarDark
                ? (vietnameseMode_ ? IDI_VIET_ON_WHITE : IDI_VIET_OFF_WHITE)
                : (vietnameseMode_ ? IDI_VIET_ON_BLACK : IDI_VIET_OFF_BLACK);
            newIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(iconId));
            break;
        }

        case IconStyle::Color:
        default:
            newIcon = LoadIconW(hInstance,
                MAKEINTRESOURCEW(vietnameseMode_ ? IDI_VIET_ON : IDI_VIET_OFF));
            break;
    }

    if (newIcon) {
        nid_.hIcon = newIcon;
    } else {
        // Fallback
        nid_.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_VIET_ON));
    }
}

HICON TrayIcon::CreateColorizedIcon(int baseIconId, COLORREF newColor) noexcept {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // DPI-aware icon loading
    int iconCx = GetSystemMetrics(SM_CXSMICON);
    int iconCy = GetSystemMetrics(SM_CYSMICON);

    HICON hBaseIcon = static_cast<HICON>(
        LoadImageW(hInstance, MAKEINTRESOURCEW(baseIconId), IMAGE_ICON, iconCx, iconCy, 0));
    if (!hBaseIcon) return nullptr;

    // Get icon bitmap data
    ICONINFO iconInfo = {};
    if (!GetIconInfo(hBaseIcon, &iconInfo)) {
        DestroyIcon(hBaseIcon);
        return nullptr;
    }

    HDC hdc = GetDC(nullptr);
    if (!hdc) {
        if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
        if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
        DestroyIcon(hBaseIcon);
        return nullptr;
    }

    // Get bitmap dimensions
    BITMAP bm = {};
    GetObject(iconInfo.hbmColor, sizeof(bm), &bm);
    int width = bm.bmWidth;
    int height = bm.bmHeight;

    // Extract pixels as 32-bit BGRA
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<BYTE> pixels(width * height * 4);

    GetDIBits(hdc, iconInfo.hbmColor, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);

    // Colorize: replace RGB while preserving alpha
    BYTE newR = GetRValue(newColor);
    BYTE newG = GetGValue(newColor);
    BYTE newB = GetBValue(newColor);

    for (int i = 0; i < width * height; i++) {
        BYTE* pixel = pixels.data() + i * 4;
        BYTE alpha = pixel[3];
        if (alpha > 0) {
            pixel[0] = newB;  // DIB is BGRA
            pixel[1] = newG;
            pixel[2] = newR;
            // alpha unchanged
        }
    }

    // Write modified pixels back
    SetDIBits(hdc, iconInfo.hbmColor, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);

    // Create new icon from modified bitmaps
    HICON hNewIcon = CreateIconIndirect(&iconInfo);

    // Cleanup GDI objects
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    DestroyIcon(hBaseIcon);

    return hNewIcon;
}

void TrayIcon::RefreshConvertHotkeyCache() {
    RefreshConvertHotkeyCache(ConfigManager::LoadConvertConfigOrDefault());
}

void TrayIcon::RefreshConvertHotkeyCache(const ConvertConfig& cc) {
    const auto& hk = cc.hotkey;
    cachedConvertHotkeyText_.clear();
    if (hk.HasAny()) {
        if (hk.ctrl)  cachedConvertHotkeyText_ += L"Ctrl+";
        if (hk.alt)   cachedConvertHotkeyText_ += L"Alt+";
        if (hk.shift) cachedConvertHotkeyText_ += L"Shift+";
        if (hk.win)   cachedConvertHotkeyText_ += L"Win+";
        if (hk.key != 0) {
            if (hk.key == L' ') {
                cachedConvertHotkeyText_ += L"Space";
            } else {
                cachedConvertHotkeyText_ += static_cast<wchar_t>(towupper(hk.key));
            }
        }
    }
}

void TrayIcon::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Query current state for checkmarks
    TrayMenuState state;
    if (menuStateGetter_) {
        state = menuStateGetter_();
    }

    auto checked = [](bool on) -> UINT { return MF_STRING | (on ? MF_CHECKED : 0); };

    // ── Section 0: Restart-to-finish-update (conditional) ──
    if (sharedState_
        && GetUpdateBannerState(*sharedState_) != UpdateBannerState::Hide) {
        AppendMenuW(hMenu, MF_STRING,
            static_cast<UINT>(TrayMenuId::RestartWindows),
            S(StringId::UPDATE_BANNER_RESTART_NOW));
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    // ── Section 1: Vietnamese mode toggle ──
    AppendMenuW(hMenu, checked(state.vietnamese),
        static_cast<UINT>(TrayMenuId::ToggleMode), S(StringId::MENU_TOGGLE_VIET));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Section 2: Feature toggles ──
    AppendMenuW(hMenu, checked(state.spellCheck),
        static_cast<UINT>(TrayMenuId::SpellCheck), S(StringId::MENU_SPELL_CHECK));
    AppendMenuW(hMenu, checked(state.smartSwitch),
        static_cast<UINT>(TrayMenuId::SmartSwitch), S(StringId::MENU_SMART_SWITCH));
    AppendMenuW(hMenu, checked(state.macroEnabled),
        static_cast<UINT>(TrayMenuId::MacroEnabled), S(StringId::MENU_MACRO_TOGGLE));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Section 3: Tools ──
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::MacroTable), S(StringId::MENU_MACRO_CONFIG));
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::ConvertTool), S(StringId::MENU_CONVERT_TOOL));

    // Quick Convert — show cached hotkey as accelerator text
    {
        std::wstring label = S(StringId::MENU_QUICK_CONVERT);
        if (!cachedConvertHotkeyText_.empty()) {
            label += L'\t';
            label += cachedConvertHotkeyText_;
        }
        AppendMenuW(hMenu, MF_STRING,
            static_cast<UINT>(TrayMenuId::QuickConvert), label.c_str());
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Section 4: Input method submenu ──
    HMENU hInputMenu = CreatePopupMenu();
    if (hInputMenu) {
        auto addRadio = [&](int method, TrayMenuId id, const wchar_t* label) {
            UINT flags = MF_STRING | (state.inputMethod == method ? MF_CHECKED : 0);
            AppendMenuW(hInputMenu, flags, static_cast<UINT>(id), label);
        };
        addRadio(0, TrayMenuId::InputTelex, L"Telex");
        addRadio(1, TrayMenuId::InputVNI, L"VNI");
        addRadio(2, TrayMenuId::InputSimpleTelex, L"Simple Telex");
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hInputMenu), S(StringId::MENU_INPUT_METHOD));
    }

    // ── Code table submenu ──
    HMENU hCodeTableMenu = CreatePopupMenu();
    if (hCodeTableMenu) {
        auto addCT = [&](CodeTable ct, TrayMenuId id, const wchar_t* label) {
            UINT flags = MF_STRING | (state.codeTable == ct ? MF_CHECKED : 0);
            AppendMenuW(hCodeTableMenu, flags, static_cast<UINT>(id), label);
        };
        addCT(CodeTable::Unicode,         TrayMenuId::CodeTableUnicode,  L"Unicode");
        addCT(CodeTable::TCVN3,           TrayMenuId::CodeTableTCVN3,    L"TCVN3 (ABC)");
        addCT(CodeTable::VNIWindows,      TrayMenuId::CodeTableVNI,      L"VNI Windows");
        addCT(CodeTable::UnicodeCompound, TrayMenuId::CodeTableCompound, S(StringId::MENU_UNICODE_COMPOUND));
        addCT(CodeTable::VietnameseLocale,TrayMenuId::CodeTableCP1258,   L"CP 1258");
        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hCodeTableMenu), S(StringId::MENU_CODE_TABLE));
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Section 5: System ──
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::Settings), S(StringId::MENU_SETTINGS));
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::About), S(StringId::MENU_ABOUT));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING,
        static_cast<UINT>(TrayMenuId::Exit), S(StringId::MENU_EXIT));

    POINT pt;
    GetCursorPos(&pt);

    // Capture the real user window before SetForegroundWindow(tray) so menu actions
    // that operate on the source window (Quick Convert → Ctrl+C) still target it.
    HWND prevFg = GetForegroundWindow();
    if (prevFg == hwndMessage_) prevFg = nullptr;

    SetForegroundWindow(hwndMessage_);

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwndMessage_, nullptr);
    DestroyMenu(hMenu);

    // Restore the source window as foreground before dispatching — otherwise
    // GetForegroundWindow() inside the callback returns our tray message window.
    if (prevFg) SetForegroundWindow(prevFg);

    if (cmd && menuCallback_) {
        menuCallback_(static_cast<TrayMenuId>(cmd));
    }
    PostMessageW(hwndMessage_, WM_NULL, 0, 0);
}

bool TrayIcon::ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept try {
    // Deferred V/E mode sync from hook callback or tray click (PostMessage pattern).
    // Settings notification is posted directly from modeChangeCallback_ (1 hop) —
    // no FindWindow needed here.
    if (msg == WM_NEXUSKEY_TRAY_MODE_SYNC && hwnd == hwndMessage_) {
        SetVietnameseMode(wParam != 0);
        return true;
    }

    // Settings dialog requesting a specific V/E mode (cross-process)
    if (msg == WM_NEXUSKEY_SET_MODE && hwnd == hwndMessage_) {
        bool vietnamese = (wParam != 0);
        if (modeRequestCallback_) {
            modeRequestCallback_(vietnamese);
        }
        // Update icon directly (callback may post deferred update,
        // but ensure immediate visual sync)
        SetVietnameseMode(vietnamese);
        return true;
    }

    // WM_CLOSE on tray window: clean shutdown (used by updater to signal exit)
    if (msg == WM_CLOSE && hwnd == hwndMessage_) {
        Destroy();
        PostQuitMessage(0);
        return true;
    }

    // Auto-update check found an available update — show TaskDialog
    if (msg == WM_NEXUSKEY_UPDATE_AVAILABLE && hwnd == hwndMessage_) {
        auto* info = reinterpret_cast<UpdateInfo*>(lParam);
        if (info) {
            if (UpdateChecker::ShowUpdateDialog(hwnd, *info)) {
                // User clicked "Update now" — download with progress dialog
                std::wstring downloadUrl = info->downloadUrl;
                delete info;

                if (UpdateChecker::DownloadWithProgress(hwnd, downloadUrl)) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
            } else {
                delete info;
            }
        }
        return true;
    }

    // Hook config changed (TSF apps, excluded apps, macros, …) — eager reload so the
    // new list applies without waiting for a keystroke / focus change in the target app.
    if (msg == WM_NEXUSKEY_HOOK_RELOAD && hwnd == hwndMessage_) {
        if (hookReloadCallback_) hookReloadCallback_();
        return true;
    }

    // System config changed (icon style, language, etc.) — re-read from TOML and refresh
    if (msg == WM_NEXUSKEY_ICON_CHANGED && hwnd == hwndMessage_) {
        auto sysConfig = ConfigManager::LoadSystemConfigOrDefault();
        SetIconConfig(sysConfig.iconStyle, sysConfig.customColorV, sysConfig.customColorE);
        SetLanguage(static_cast<Language>(sysConfig.language));
        RefreshConvertHotkeyCache();
        if (iconConfigChangedCallback_) iconConfigChangedCallback_();
        return true;
    }

    // Restart app (admin mode changed in settings)
    if (msg == WM_NEXUSKEY_RESTART && hwnd == hwndMessage_) {
        switch (RestartWithNewAdminMode()) {
            case AdminRestartResult::Restarting:
                // New instance launched — exit via menu callback
                // (TerminateAllSubprocesses is called inside OnMenuCommand::Exit)
                if (menuCallback_) {
                    menuCallback_(TrayMenuId::Exit);
                }
                break;
            case AdminRestartResult::DeElevationFailed:
                // Config was saved as runAsAdmin=false but we couldn't spawn an
                // unelevated child (no shell). Tell the user to restart manually
                // so they don't think the toggle silently failed.
                MessageBoxW(nullptr, S(StringId::ADMIN_DEELEVATION_FAILED),
                             L"NexusKey", MB_ICONWARNING | MB_OK);
                break;
            case AdminRestartResult::NoRestartNeeded:
            case AdminRestartResult::UacDenied:
                // Nothing to do; UacDenied already reverted config inside helper.
                break;
        }
        return true;
    }

    // Another instance tried to start and requests to show Settings
    if (msg == WM_NEXUSKEY_SHOW_SETTINGS && hwnd == hwndMessage_) {
        if (menuCallback_) {
            menuCallback_(TrayMenuId::Settings);
        }
        return true;
    }

    // WM_HOTKEY is handled by the caller's WndProc, not here
    if (msg == WM_HOTKEY && hwnd == hwndMessage_) {
        if (menuCallback_) {
            menuCallback_(TrayMenuId::ToggleMode);
        }
        return true;
    }

    if (msg != WM_TRAYICON || hwnd != hwndMessage_) return false;

    switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu();
            return true;
        case WM_LBUTTONUP:
            // After double-click, Windows sends a trailing WM_LBUTTONUP — skip it
            if (ignoreNextLButtonUp_) {
                ignoreNextLButtonUp_ = false;
                return true;
            }
            // Toggle immediately (no delay)
            toggledByClick_ = true;
            if (menuCallback_) {
                menuCallback_(TrayMenuId::ToggleMode);
            }
            return true;
        case WM_LBUTTONDBLCLK:
            // Undo the toggle from the first click, then open settings
            if (toggledByClick_ && menuCallback_) {
                menuCallback_(TrayMenuId::ToggleMode);  // Undo
            }
            toggledByClick_ = false;
            ignoreNextLButtonUp_ = true;  // Suppress trailing WM_LBUTTONUP
            if (menuCallback_) {
                menuCallback_(TrayMenuId::Settings);
            }
            return true;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case NIN_POPUPOPEN:
        case NIN_POPUPCLOSE:
        case NIN_KEYSELECT:
        case NIN_SELECT:
        case WM_MOUSEHOVER:
        case WM_MOUSELEAVE:
            // Ignore benign events so they don't reset the click tracking state
            return true;
    }

    // Any other event (e.g., focus change, other buttons) clears the toggle tracking
    toggledByClick_ = false;
    ignoreNextLButtonUp_ = false;
    return false;
} catch (const std::exception& e) {
    CrashLog(L"TrayIcon::ProcessMessage", e.what());
    return false;
} catch (...) {
    CrashLog(L"TrayIcon::ProcessMessage", "(non-std exception)");
    return false;
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Top-level catch: WndProc is kernel-dispatched via KiUserCallbackDispatcher.
    // An escaping C++ exception becomes STATUS_FATAL_USER_CALLBACK_EXCEPTION
    // (0xC000041D) and terminates the process — issue #103.
    try {
        // Real-time theme switch for context menus + Auto tray icon (issue #104)
        if (msg == WM_SETTINGCHANGE && lParam) {
            if (wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
                bool dark = DarkModeHelper::IsWindowsDarkMode();
                DarkModeHelper::SetWindowDarkMode(hwnd, dark);

                if (g_trayInstance &&
                    static_cast<IconStyle>(g_trayInstance->iconStyle_) == IconStyle::Auto) {
                    g_trayInstance->RefreshIcon();
                    if (g_trayInstance->nid_.hIcon) {
                        Shell_NotifyIconW(NIM_MODIFY, &g_trayInstance->nid_);
                    }
                }
            }
        }

        // Handle TaskbarCreated: explorer.exe restarted, re-add our tray icon
        if (g_trayInstance && g_trayInstance->wmTaskbarCreated_ != 0 &&
            msg == g_trayInstance->wmTaskbarCreated_) {
            g_trayInstance->ReAddIcon();
            return 0;
        }

        if (g_trayInstance && g_trayInstance->ProcessMessage(hwnd, msg, wParam, lParam)) {
            return TRUE;
        }
    } catch (const std::exception& e) {
        CrashLog(L"TrayIcon::WndProc", e.what());
    } catch (...) {
        CrashLog(L"TrayIcon::WndProc", "(non-std exception)");
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey
