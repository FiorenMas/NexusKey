// VKey - Lightweight Toast Notification Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "ToastPopup.h"
#include "DarkModeHelper.h"
#include "../resource.h"
#include "core/CrashLog.h"
#include <exception>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace NextKey {

bool ToastPopup::s_classRegistered = false;

static constexpr UINT_PTR TIMER_DISMISS = 1;
static constexpr BYTE TOAST_ALPHA = 230;
static constexpr COLORREF ACCENT_COLOR = RGB(0, 120, 212);

// Layout constants
static constexpr int TOAST_WIDTH = 280;
static constexpr int TITLE_HEIGHT = 28;
static constexpr int BODY_HEIGHT = 36;
static constexpr int TOAST_HEIGHT = TITLE_HEIGHT + BODY_HEIGHT;
static constexpr int PADDING_X = 12;
static constexpr int ICON_SIZE = 20;
static constexpr int ICON_GAP = 8;
static constexpr int ACCENT_LINE_H = 2;

// Dark theme colors
namespace Dark {
    static constexpr COLORREF BG      = RGB(30, 30, 30);
    static constexpr COLORREF TITLE   = RGB(180, 180, 180);
    static constexpr COLORREF MSG     = RGB(255, 255, 255);
    static constexpr COLORREF BORDER  = RGB(70, 70, 70);
}

// Light theme colors
namespace Light {
    static constexpr COLORREF BG      = RGB(249, 249, 249);
    static constexpr COLORREF TITLE   = RGB(100, 100, 100);
    static constexpr COLORREF MSG     = RGB(20, 20, 20);
    static constexpr COLORREF BORDER  = RGB(200, 200, 200);
}

// Theme detection uses DarkModeHelper::IsWindowsDarkMode()

void ToastPopup::RegisterWindowClass() {
    if (s_classRegistered) return;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VKeyToast";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    RegisterClassExW(&wc);
    s_classRegistered = true;
}

void ToastPopup::Show(const std::wstring& message, DWORD durationMs) {
    RegisterWindowClass();

    // DPI-scale window size for PerMonitorV2 (coordinates are physical pixels)
    auto pfnGetDpiForSystem = reinterpret_cast<UINT(WINAPI*)()>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    const int dpi = pfnGetDpiForSystem ? static_cast<int>(pfnGetDpiForSystem()) : 96;
    const int w = MulDiv(TOAST_WIDTH, dpi, 96);
    const int h = MulDiv(TOAST_HEIGHT, dpi, 96);
    const int margin = MulDiv(12, dpi, 96);

    // Position at bottom-right of the active monitor's work area
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    HMONITOR hMon = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);

    int x = mi.rcWork.right - w - margin;
    int y = mi.rcWork.bottom - h - margin;

    auto* msgCopy = new std::wstring(message);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"VKeyToast", nullptr,
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr),
        msgCopy);

    if (!hwnd) {
        delete msgCopy;
        return;
    }

    // Store initial theme
    SetPropW(hwnd, L"dark", reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(DarkModeHelper::IsWindowsDarkMode() ? 1 : 0)));

    SetLayeredWindowAttributes(hwnd, 0, TOAST_ALPHA, LWA_ALPHA);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    SetTimer(hwnd, TIMER_DISMISS, durationMs, nullptr);

    // Pump messages until PostQuitMessage(0) in WM_DESTROY causes GetMessageW to return 0.
    // IMPORTANT: Do NOT break early with IsWindow() check — that leaves a stale WM_QUIT
    // in the thread's message queue, which poisons any subsequent COM/message-loop operation
    // on this thread (e.g. URLDownloadToFileW in the update flow).
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void PaintToast(HWND hwnd, HDC hdc, bool dark) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    COLORREF bgColor     = dark ? Dark::BG     : Light::BG;
    COLORREF titleColor  = dark ? Dark::TITLE   : Light::TITLE;
    COLORREF msgColor    = dark ? Dark::MSG     : Light::MSG;
    COLORREF borderColor = dark ? Dark::BORDER  : Light::BORDER;

    // Fill background
    HBRUSH hBgBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &rc, hBgBrush);
    DeleteObject(hBgBrush);

    // Blue accent line at top
    RECT accentRect = { rc.left, rc.top, rc.right, rc.top + ACCENT_LINE_H };
    HBRUSH hAccentBrush = CreateSolidBrush(ACCENT_COLOR);
    FillRect(hdc, &accentRect, hAccentBrush);
    DeleteObject(hAccentBrush);

    // Border
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
    HBRUSH hNullBrush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    SelectObject(hdc, hNullBrush);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);

    // ── Title row: "VKey" ──
    HFONT hTitleFont = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hTitleFont));
    SetTextColor(hdc, titleColor);

    RECT titleRect = { PADDING_X, ACCENT_LINE_H, rc.right - PADDING_X, TITLE_HEIGHT + ACCENT_LINE_H };
    DrawTextW(hdc, L"VKey", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
    DeleteObject(hTitleFont);

    // ── Body row: info icon + message ──
    auto* msgStr = static_cast<std::wstring*>(GetPropW(hwnd, L"msg"));
    if (msgStr) {
        int bodyTop = TITLE_HEIGHT;

        // Draw info icon (shell stock icon)
        HICON hInfoIcon = nullptr;
        SHSTOCKICONINFO sii = { sizeof(sii) };
        if (SUCCEEDED(SHGetStockIconInfo(SIID_INFO, SHGSI_ICON | SHGSI_SMALLICON, &sii))) {
            hInfoIcon = sii.hIcon;
        }

        int iconX = PADDING_X;
        int iconY = bodyTop + (BODY_HEIGHT - ICON_SIZE) / 2;
        int textLeft = iconX;

        if (hInfoIcon) {
            DrawIconEx(hdc, iconX, iconY, hInfoIcon, ICON_SIZE, ICON_SIZE,
                       0, nullptr, DI_NORMAL);
            DestroyIcon(hInfoIcon);
            textLeft = iconX + ICON_SIZE + ICON_GAP;
        }

        // Message text
        HFONT hMsgFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hOldFont2 = static_cast<HFONT>(SelectObject(hdc, hMsgFont));
        SetTextColor(hdc, msgColor);

        RECT msgRect = { textLeft, bodyTop, rc.right - PADDING_X, bodyTop + BODY_HEIGHT };
        DrawTextW(hdc, msgStr->c_str(), static_cast<int>(msgStr->size()), &msgRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, hOldFont2);
        DeleteObject(hMsgFont);
    }
}

LRESULT CALLBACK ToastPopup::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) try {
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            auto* msgStr = static_cast<std::wstring*>(cs->lpCreateParams);
            SetPropW(hwnd, L"msg", msgStr);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            bool dark = reinterpret_cast<LONG_PTR>(GetPropW(hwnd, L"dark")) != 0;
            PaintToast(hwnd, hdc, dark);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETTINGCHANGE: {
            // Real-time theme switch: Windows broadcasts this when user changes theme
            if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
                bool newDark = DarkModeHelper::IsWindowsDarkMode();
                bool oldDark = reinterpret_cast<LONG_PTR>(GetPropW(hwnd, L"dark")) != 0;
                if (newDark != oldDark) {
                    SetPropW(hwnd, L"dark", reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(newDark ? 1 : 0)));
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_DISMISS) {
                KillTimer(hwnd, TIMER_DISMISS);
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_DESTROY: {
            auto* msgStr = static_cast<std::wstring*>(GetPropW(hwnd, L"msg"));
            delete msgStr;
            RemovePropW(hwnd, L"msg");
            RemovePropW(hwnd, L"dark");
            PostQuitMessage(0); // Break the message loop in Show()
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
} catch (const std::exception& e) {
    CrashLog(L"ToastPopup::WndProc", e.what());
    return DefWindowProcW(hwnd, msg, wParam, lParam);
} catch (...) {
    CrashLog(L"ToastPopup::WndProc", "(non-std exception)");
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey
