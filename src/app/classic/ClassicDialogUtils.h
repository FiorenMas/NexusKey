// VKey Classic — Shared dialog utilities
// Process enumeration, window picking, file dialogs
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <TlHelp32.h>
#include <commdlg.h>
#include <CommCtrl.h>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>
#include <fstream>

namespace NextKey::Classic {

// ═══════════════════════════════════════════════════════════
// DPI helpers — per-window DPI scaling for Classic dialogs
// ═══════════════════════════════════════════════════════════

/// Get per-window DPI (Win10 1607+), falls back to system DPI.
[[nodiscard]] inline UINT GetWindowDpi(HWND hwnd) noexcept {
    auto pfn = reinterpret_cast<UINT(WINAPI*)(HWND)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (pfn && hwnd) {
        UINT dpi = pfn(hwnd);
        if (dpi > 0) return dpi;
    }
    HDC hdc = GetDC(nullptr);
    if (!hdc) return 96;
    UINT dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
    ReleaseDC(nullptr, hdc);
    return dpi ? dpi : 96;
}

/// Scale a value by DPI (96 = 100%).
[[nodiscard]] inline int DpiScale(int value, UINT dpi) noexcept {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

// ═══════════════════════════════════════════════════════════
// String helpers
// ═══════════════════════════════════════════════════════════

inline std::wstring ToLowerAscii(const std::wstring& s) {
    std::wstring result = s;
    for (auto& c : result) {
        if (c >= L'A' && c <= L'Z') c += 32;
    }
    return result;
}

// ═══════════════════════════════════════════════════════════
// Process enumeration
// ═══════════════════════════════════════════════════════════

/// Get sorted, deduplicated list of running process names (lowercase).
/// Filters out system processes and VKey itself.
inline std::vector<std::wstring> GetRunningApps() {
    std::vector<std::wstring> apps;
    std::unordered_set<std::wstring> seen;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return apps;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name = ToLowerAscii(pe.szExeFile);

            // Skip system processes
            if (name == L"system" || name == L"system idle process" ||
                name == L"svchost.exe" || name == L"csrss.exe" ||
                name == L"smss.exe" || name == L"wininit.exe" ||
                name == L"services.exe" || name == L"lsass.exe" ||
                name == L"conhost.exe" || name == L"dwm.exe" ||
                name == L"vkey.exe" || name == L"vkeylite.exe" ||
                name == L"[system process]") {
                continue;
            }

            if (seen.insert(name).second) {
                apps.push_back(std::move(name));
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    std::sort(apps.begin(), apps.end());
    return apps;
}

/// Get exe name from a window handle (lowercase). Returns empty on failure.
inline std::wstring GetExeNameFromWindow(HWND hwnd) {
    if (!hwnd) return {};

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (!processId) return {};

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) return {};

    wchar_t exePath[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(hProcess, 0, exePath, &size)) {
        CloseHandle(hProcess);
        return {};
    }
    CloseHandle(hProcess);

    const wchar_t* filename = wcsrchr(exePath, L'\\');
    return ToLowerAscii(filename ? filename + 1 : exePath);
}

// ═══════════════════════════════════════════════════════════
// Window Picker — crosshair cursor to pick a window
// ═══════════════════════════════════════════════════════════

/// Manages the window-picking state machine.
/// Usage: call Start(), handle WM_SETCURSOR/WM_LBUTTONUP/WM_KEYDOWN
/// in your WndProc, call HandleMessage(). On pick, callback fires.
class WindowPicker {
public:
    using Callback = std::function<void(const std::wstring& exeName)>;

    void Start(HWND ownerHwnd, Callback cb) {
        if (picking_) return;
        picking_ = true;
        owner_ = ownerHwnd;
        callback_ = std::move(cb);

        SetCapture(owner_);

        // Save original cursor and replace with crosshair
        HCURSOR hArrow = LoadCursorW(nullptr, IDC_ARROW);
        savedCursor_ = CopyCursor(hArrow);
        HCURSOR hCross = LoadCursorW(nullptr, IDC_CROSS);
        SetSystemCursor(CopyCursor(hCross), 32512 /*OCR_NORMAL*/);
    }

    void Stop() {
        if (!picking_) return;
        picking_ = false;
        ReleaseCapture();

        if (savedCursor_) {
            SetSystemCursor(savedCursor_, 32512);
            savedCursor_ = nullptr;
        }
        if (owner_) SetForegroundWindow(owner_);
    }

    /// Returns true if message was handled.
    bool HandleMessage(UINT msg, WPARAM wParam) {
        if (!picking_) return false;

        if (msg == WM_SETCURSOR) {
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return true;
        }

        if (msg == WM_LBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HWND target = WindowFromPoint(pt);
            if (target) target = GetAncestor(target, GA_ROOT);

            Stop();

            if (target && target != owner_) {
                std::wstring exe = GetExeNameFromWindow(target);
                if (!exe.empty() && callback_) {
                    callback_(exe);
                }
            }
            return true;
        }

        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            Stop();
            return true;
        }

        return false;
    }

    [[nodiscard]] bool IsPicking() const noexcept { return picking_; }

private:
    bool picking_ = false;
    HWND owner_ = nullptr;
    HCURSOR savedCursor_ = nullptr;
    Callback callback_;
};

// ═══════════════════════════════════════════════════════════
// File dialog helpers
// ═══════════════════════════════════════════════════════════

/// Open file dialog, returns selected path or empty.
inline std::wstring OpenFileDialog(HWND parent, const wchar_t* filter, const wchar_t* title) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) return path;
    return {};
}

/// Save file dialog, returns selected path or empty.
inline std::wstring SaveFileDialog(HWND parent, const wchar_t* filter, const wchar_t* title, const wchar_t* defaultExt) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameW(&ofn)) return path;
    return {};
}

// ═══════════════════════════════════════════════════════════
// UTF-8 file I/O helpers
// ═══════════════════════════════════════════════════════════

inline std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), len);
    return result;
}

inline std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}

}  // namespace NextKey::Classic

#endif  // _WIN32
