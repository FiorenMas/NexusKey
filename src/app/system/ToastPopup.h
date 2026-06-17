// VKey - Lightweight Toast Notification
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>
#include <Windows.h>

namespace NextKey {

/// Simple Win32 layered window toast popup (no Sciter dependency).
/// Displays a brief notification near the cursor, auto-dismisses.
class ToastPopup {
public:
    static void Show(const std::wstring& message, DWORD durationMs = 1500);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void RegisterWindowClass();
    static bool s_classRegistered;
};

}  // namespace NextKey
