// VKey - Window Picker Base Dialog Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "WindowPickerDialog.h"
#include "helpers/AppHelpers.h"
#include <algorithm>
#include <unordered_set>
#include <TlHelp32.h>

namespace NextKey {

std::vector<std::wstring> WindowPickerDialog::getRunningApps() {
    std::unordered_set<std::wstring> seen;
    std::vector<std::wstring> apps;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return apps;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            std::wstring name = ToLowerAscii(pe.szExeFile);
            if (name == L"system" || name == L"system idle process" ||
                name == L"svchost.exe" || name == L"csrss.exe" ||
                name == L"smss.exe" || name == L"wininit.exe" ||
                name == L"services.exe" || name == L"lsass.exe" ||
                name == L"conhost.exe" || name == L"dwm.exe" ||
                name == L"vkey.exe" || name == L"[system process]") {
                continue;
            }
            if (seen.insert(name).second) {
                apps.push_back(name);
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    std::sort(apps.begin(), apps.end());
    return apps;
}

std::wstring WindowPickerDialog::getExeNameFromWindow(HWND hwnd) {
    if (!hwnd) return L"";

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) return L"";

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) return L"";

    wchar_t exePath[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(hProcess, 0, exePath, &size)) {
        const wchar_t* filename = wcsrchr(exePath, L'\\');
        result = ToLowerAscii(filename ? filename + 1 : exePath);
    }
    CloseHandle(hProcess);
    return result;
}

void WindowPickerDialog::startWindowPicking() {
    isPickingWindow_ = true;
    SetCapture(get_hwnd());

    // Save original arrow cursor before replacing
    HCURSOR hOriginalArrow = LoadCursor(nullptr, IDC_ARROW);
    savedArrowCursor_ = CopyCursor(hOriginalArrow);

    // Replace system arrow cursor with crosshair globally
    HCURSOR hCross = LoadCursor(nullptr, IDC_CROSS);
    SetSystemCursor(CopyCursor(hCross), OCR_NORMAL);
}

void WindowPickerDialog::stopWindowPicking() {
    isPickingWindow_ = false;
    ReleaseCapture();

    // Restore original arrow cursor (SetSystemCursor takes ownership of the handle)
    if (savedArrowCursor_) {
        SetSystemCursor(savedArrowCursor_, OCR_NORMAL);
        savedArrowCursor_ = nullptr;
    }

    SetForegroundWindow(get_hwnd());
}

LRESULT WindowPickerDialog::onCustomMessage(HWND hwnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam) {
    // Window Picker: show crosshair cursor continuously
    if (msg == WM_SETCURSOR && isPickingWindow_) {
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        return TRUE;
    }

    // Window Picker: click to capture target window
    if (msg == WM_LBUTTONUP && isPickingWindow_) {
        POINT pt;
        GetCursorPos(&pt);
        HWND targetWnd = WindowFromPoint(pt);
        if (targetWnd) targetWnd = GetAncestor(targetWnd, GA_ROOT);

        if (targetWnd && targetWnd != hwnd) {
            std::wstring exeName = getExeNameFromWindow(targetWnd);
            if (!exeName.empty()) {
                stopWindowPicking();
                onWindowPicked(exeName);
                return 0;
            }
        }
        stopWindowPicking();
        return 0;
    }

    // Window Picker: ESC to cancel
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && isPickingWindow_) {
        stopWindowPicking();
        return 0;
    }

    return -1;  // Unhandled
}

}  // namespace NextKey
