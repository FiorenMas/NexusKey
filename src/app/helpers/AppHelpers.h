// VKey - App-Layer Helper Functions
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "core/config/ConfigEvent.h"
#include "core/ipc/SharedStateManager.h"
#ifdef _WIN32
#include "core/ipc/SharedConstants.h"
#endif
#include <cwctype>
#include <istream>
#include <string>

#ifndef _WIN32
// Stub for non-Windows builds (Linux tests)
inline void InstallCursorCrashHandler() noexcept {}
#else
#include <Windows.h>

/// Crash handler that restores system cursors if app crashes during window picking.
/// SetSystemCursor() changes cursors globally — if we crash mid-pick, the crosshair
/// cursor stays until logoff. This handler restores defaults on any unhandled exception.
inline LONG WINAPI CursorCrashHandler(EXCEPTION_POINTERS*) noexcept {
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
    return EXCEPTION_CONTINUE_SEARCH;  // Let debugger/WER handle it
}

/// Install the cursor crash handler. Call once at startup (main.cpp / main_lite.cpp).
inline void InstallCursorCrashHandler() noexcept {
    SetUnhandledExceptionFilter(CursorCrashHandler);
}
#endif

namespace NextKey {

/// Signal HookEngine that a config value changed.
/// Called from dialog persistence methods after ConfigManager::Save*().
/// Increments configGeneration in SharedState — HookEngine detects on next keystroke.
/// Also signals ConfigEvent for TSF DLL which still uses the Named Event path.
inline void SignalConfigChange() noexcept {
    // Bump configGeneration in SharedState (HookEngine reads this)
    SharedStateManager sm;
    if (sm.OpenReadWrite()) {
        SharedState state = sm.Read();
        if (state.IsValid()) {
            state.configGeneration++;
            sm.Write(state);
        }
    }
    // Signal ConfigEvent for TSF DLL (still uses Named Event)
    ConfigEvent event;
    if (event.Initialize()) {
        event.Signal();
    }
#ifdef _WIN32
    // Eager hook reload: tell main EXE to QuickSync now so new list applies
    // without waiting for the next keystroke / focus change in the target app.
    if (HWND trayWnd = FindWindowW(L"VKeyTrayClass", nullptr)) {
        PostMessageW(trayWnd, WM_VKEY_HOOK_RELOAD, 0, 0);
    }
#endif
}

/// Convert wstring to lowercase (ASCII-safe, for app names and macro keys).
inline std::wstring ToLowerAscii(std::wstring str) noexcept {
    for (auto& c : str) c = towlower(c);
    return str;
}

/// Iterate UTF-8 text lines from a stream. Strips trailing CR (Windows
/// CRLF) + UTF-8 BOM on the first line (Notepad-saved files). Skips
/// empty lines and lines starting with ';' (comment). Caller owns the
/// stream + decides what to do with each non-empty content line. Used
/// by dialog Import handlers — keeps the open/close/error-handling per
/// call site since UX varies (Sciter silent on open-fail; Classic shows
/// MessageBox).
template <typename Handler>
inline void ParseConfigLines(std::istream& input, Handler handler) {
    std::string line;
    bool firstLine = true;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (firstLine) {
            firstLine = false;
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }
        if (line.empty() || line[0] == ';') continue;
        handler(line);
    }
}

#ifdef _WIN32
/// Get the focused child window within a foreground top-level HWND.
/// Uses AttachThreadInput for cross-thread queries — SLOW, avoid per-keystroke.
/// Returns nullptr if GetFocus() fails or foreground is null.
[[nodiscard]] inline HWND GetFocusedChildHwnd(HWND foreground) noexcept {
    if (!foreground) return nullptr;
    DWORD fgTid = GetWindowThreadProcessId(foreground, nullptr);
    DWORD myTid = GetCurrentThreadId();
    if (fgTid == myTid) return GetFocus();
    HWND focused = nullptr;
    if (AttachThreadInput(myTid, fgTid, TRUE)) {
        focused = GetFocus();
        AttachThreadInput(myTid, fgTid, FALSE);
    }
    return focused;
}

/// Same as GetFocusedChildHwnd, but falls back to the foreground HWND itself
/// when no child is focused. Useful when the caller wants to operate on
/// *something* (e.g. send EM_GETSEL) rather than return failure.
[[nodiscard]] inline HWND GetFocusedChildOrForeground(HWND foreground) noexcept {
    HWND focused = GetFocusedChildHwnd(foreground);
    return focused ? focused : foreground;
}

/// Multi-monitor-aware top-left for centering a window of given size on the
/// monitor that contains `referenceHwnd`. Falls back to the primary monitor
/// when the reference HWND is null/invalid. Honors taskbar / docked panels
/// by using `rcWork` instead of `rcMonitor`. Replaces the old
/// `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)` pattern, which always
/// centers on the primary screen and ignores the work area.
[[nodiscard]] inline POINT GetCenteredPos(HWND referenceHwnd, int width, int height) noexcept {
    HMONITOR mon = MonitorFromWindow(referenceHwnd ? referenceHwnd : GetDesktopWindow(),
                                     MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi)) {
        // Last-resort: primary screen metrics, ignore work area.
        return { (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
                 (GetSystemMetrics(SM_CYSCREEN) - height) / 2 };
    }
    const int sx = mi.rcWork.right - mi.rcWork.left;
    const int sy = mi.rcWork.bottom - mi.rcWork.top;
    return { mi.rcWork.left + (sx - width) / 2,
             mi.rcWork.top + (sy - height) / 2 };
}
#endif

}  // namespace NextKey
