// NexusKey - App-Layer Helper Functions
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "core/config/ConfigEvent.h"
#include "core/ipc/SharedStateManager.h"
#ifdef _WIN32
#include "core/ipc/SharedConstants.h"
#endif
#include <cwctype>
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
    if (HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr)) {
        PostMessageW(trayWnd, WM_NEXUSKEY_HOOK_RELOAD, 0, 0);
    }
#endif
}

/// Convert wstring to lowercase (ASCII-safe, for app names and macro keys).
inline std::wstring ToLowerAscii(std::wstring str) noexcept {
    for (auto& c : str) c = towlower(c);
    return str;
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
#endif

}  // namespace NextKey
