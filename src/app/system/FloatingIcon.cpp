// NexusKey - Floating V/E Icon Overlay Implementation
// SPDX-License-Identifier: GPL-3.0-only
//
// Pure GDI + direct pixel math — no GDI+ dependency.
// Draggable via WM_NCHITTEST → HTCAPTION.

#include "FloatingIcon.h"
#include "core/Debug.h"
#include "core/CrashLog.h"

#include <cmath>
#include <exception>
#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM

namespace NextKey {

static FloatingIcon* g_floatingInstance = nullptr;

// Colors (RGB components)
static constexpr uint8_t V_R = 0xE5, V_G = 0x39, V_B = 0x35;  // Red
static constexpr uint8_t E_R = 0x1E, E_G = 0x88, E_B = 0xE5;  // Blue

static inline uint8_t Clamp255(float v) noexcept {
    return static_cast<uint8_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
}

/// Render a single icon bitmap (V or E) into a 32-bit premultiplied-ARGB DIB.
static HBITMAP RenderIconBitmap(uint8_t bgR, uint8_t bgG, uint8_t bgB,
                                const wchar_t* letter) {
    const int S = FloatingIcon::ICON_SIZE;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = S;
    bmi.bmiHeader.biHeight = -S;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    uint32_t* px = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS,
                                    reinterpret_cast<void**>(&px), nullptr, 0);
    if (!hBmp) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return nullptr;
    }

    // ── Anti-aliased circle via distance field ──────────────────
    const float cx = S * 0.5f;
    const float cy = S * 0.5f;
    const float outerR = S * 0.5f - 0.5f;
    const float innerR = outerR - 1.0f;

    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float dx = x + 0.5f - cx;
            float dy = y + 0.5f - cy;
            float d = std::sqrt(dx * dx + dy * dy);

            float main = 0.0f;
            if (d <= innerR - 0.5f)
                main = 1.0f;
            else if (d < innerR + 0.5f)
                main = innerR + 0.5f - d;

            float shadow = 0.0f;
            if (d > innerR - 0.5f && d <= outerR + 0.5f) {
                float ring = 1.0f;
                if (d > outerR - 0.5f)
                    ring = outerR + 0.5f - d;
                shadow = ring * 0.4f * (1.0f - main);
            }

            float a = main + shadow * (1.0f - main);
            px[y * S + x] = (Clamp255(a * 255.0f) << 24) |
                             (Clamp255(bgR * main) << 16) |
                             (Clamp255(bgG * main) << 8) |
                              Clamp255(bgB * main);
        }
    }

    // ── Letter via GDI DrawText (white on black → R = coverage) ─
    uint32_t* textPx = nullptr;
    HBITMAP hText = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS,
                                     reinterpret_cast<void**>(&textPx), nullptr, 0);
    if (hText) {
        HDC hdcText = CreateCompatibleDC(hdcScreen);
        HGDIOBJ oldText = SelectObject(hdcText, hText);

        RECT rc = {0, 0, S, S};
        FillRect(hdcText, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        // Use "Segoe UI" — available on Win7+. "Segoe UI Variable Text" is Win11-only
        // and CreateFontW silently substitutes (never returns NULL), so fallback was dead code.
        HFONT hFont = CreateFontW(
            12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        HGDIOBJ oldFont = SelectObject(hdcText, hFont);
        SetTextColor(hdcText, RGB(255, 255, 255));
        SetBkMode(hdcText, TRANSPARENT);
        DrawTextW(hdcText, letter, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        GdiFlush();

        for (int i = 0; i < S * S; i++) {
            uint8_t textAlpha = (textPx[i] >> 16) & 0xFF;
            if (textAlpha == 0) continue;

            float ta = textAlpha / 255.0f;
            uint8_t ba = (px[i] >> 24);
            uint8_t br = (px[i] >> 16) & 0xFF;
            uint8_t bg = (px[i] >> 8) & 0xFF;
            uint8_t bb =  px[i] & 0xFF;

            float ita = 1.0f - ta;
            float outA = ta + (ba / 255.0f) * ita;
            px[i] = (Clamp255(outA * 255.0f) << 24) |
                     (Clamp255(255.0f * ta + br * ita) << 16) |
                     (Clamp255(255.0f * ta + bg * ita) << 8) |
                      Clamp255(255.0f * ta + bb * ita);
        }

        SelectObject(hdcText, oldFont);
        DeleteObject(hFont);
        SelectObject(hdcText, oldText);
        DeleteDC(hdcText);
        DeleteObject(hText);
    }

    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return hBmp;
}

FloatingIcon::~FloatingIcon() {
    Destroy();
}

bool FloatingIcon::Create(HINSTANCE hInstance, bool initialVietnamese) {
    vietnameseMode_ = initialVietnamese;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NexusKeyFloatingIcon";

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

    // No WS_EX_TRANSPARENT — window accepts mouse input for drag
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"NexusKeyFloatingIcon",
        L"",
        WS_POPUP,
        0, 0, ICON_SIZE, ICON_SIZE,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd_)
        return false;

    g_floatingInstance = this;
    RenderCachedBitmaps();

    if (posX_ == INT32_MIN || posY_ == INT32_MIN)
        ComputeDefaultPosition();
    ClampToWorkArea();

    SetWindowPos(hwnd_, HWND_TOPMOST, posX_, posY_, ICON_SIZE, ICON_SIZE,
                 SWP_NOACTIVATE | SWP_NOSIZE);
    ApplyBitmap();

    NEXTKEY_LOG(L"FloatingIcon created at (%d,%d)", posX_, posY_);
    return true;
}

void FloatingIcon::Destroy() noexcept {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (bmpV_) { DeleteObject(bmpV_); bmpV_ = nullptr; }
    if (bmpE_) { DeleteObject(bmpE_); bmpE_ = nullptr; }
    visible_ = false;
    if (g_floatingInstance == this) {
        g_floatingInstance = nullptr;
    }
}

void FloatingIcon::RenderCachedBitmaps() noexcept {
    if (bmpV_) DeleteObject(bmpV_);
    if (bmpE_) DeleteObject(bmpE_);
    bmpV_ = RenderIconBitmap(V_R, V_G, V_B, L"V");
    bmpE_ = RenderIconBitmap(E_R, E_G, E_B, L"E");
}

void FloatingIcon::SetVietnameseMode(bool enabled) noexcept {
    if (vietnameseMode_ == enabled) return;
    vietnameseMode_ = enabled;
    if (visible_ && hwnd_) {
        ApplyBitmap();
    }
}

void FloatingIcon::SetVisible(bool visible) noexcept {
    if (visible_ == visible) return;
    visible_ = visible;
    if (hwnd_) {
        ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (visible) {
            ApplyBitmap();
        }
    }
}

void FloatingIcon::SetPosition(int x, int y) noexcept {
    posX_ = x;
    posY_ = y;
    if (hwnd_) {
        if (posX_ == INT32_MIN || posY_ == INT32_MIN)
            ComputeDefaultPosition();
        ClampToWorkArea();
        SetWindowPos(hwnd_, HWND_TOPMOST, posX_, posY_, ICON_SIZE, ICON_SIZE,
                     SWP_NOACTIVATE | SWP_NOSIZE);
        if (visible_) ApplyBitmap();
    }
}

void FloatingIcon::ComputeDefaultPosition() noexcept {
    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    posX_ = workArea.right - ICON_SIZE - MARGIN;
    posY_ = workArea.bottom - ICON_SIZE - MARGIN;
}

void FloatingIcon::ClampToWorkArea() noexcept {
    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int maxX = workArea.right - ICON_SIZE;
    int maxY = workArea.bottom - ICON_SIZE;
    if (posX_ < workArea.left) posX_ = workArea.left;
    if (posY_ < workArea.top) posY_ = workArea.top;
    if (posX_ > maxX) posX_ = maxX;
    if (posY_ > maxY) posY_ = maxY;
}

void FloatingIcon::ApplyBitmap() noexcept {
    if (!hwnd_) return;

    HBITMAP bmp = vietnameseMode_ ? bmpV_ : bmpE_;
    if (!bmp) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HGDIOBJ oldBmp = SelectObject(hdcMem, bmp);

    RECT rc;
    GetWindowRect(hwnd_, &rc);
    POINT ptPos = { rc.left, rc.top };
    SIZE sizeWnd = { ICON_SIZE, ICON_SIZE };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 220;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd_, hdcScreen, &ptPos, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, oldBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

LRESULT CALLBACK FloatingIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) try {
    switch (msg) {
    case WM_NCHITTEST: {
        // Hit-test: only the circle area is draggable, rest is click-through
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        float dx = pt.x + 0.5f - FloatingIcon::ICON_SIZE * 0.5f;
        float dy = pt.y + 0.5f - FloatingIcon::ICON_SIZE * 0.5f;
        float r = FloatingIcon::ICON_SIZE * 0.5f;
        if (dx * dx + dy * dy <= r * r)
            return HTCAPTION;       // Draggable
        return HTTRANSPARENT;       // Click-through outside circle
    }

    case WM_NCLBUTTONDBLCLK:
        return 0;  // Ignore double-click (don't maximize)

    case WM_EXITSIZEMOVE:
        // User finished dragging — update in-memory position.
        // Persisted to TOML at app exit only (no file I/O on drag).
        if (g_floatingInstance) {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            g_floatingInstance->posX_ = rc.left;
            g_floatingInstance->posY_ = rc.top;
            g_floatingInstance->ApplyBitmap();
        }
        return 0;

    case WM_DISPLAYCHANGE:
        if (g_floatingInstance) {
            g_floatingInstance->ClampToWorkArea();
            if (g_floatingInstance->hwnd_) {
                SetWindowPos(g_floatingInstance->hwnd_, HWND_TOPMOST,
                             g_floatingInstance->posX_, g_floatingInstance->posY_,
                             FloatingIcon::ICON_SIZE, FloatingIcon::ICON_SIZE,
                             SWP_NOACTIVATE | SWP_NOSIZE);
                if (g_floatingInstance->visible_)
                    g_floatingInstance->ApplyBitmap();
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
} catch (const std::exception& e) {
    CrashLog(L"FloatingIcon::WndProc", e.what());
    return DefWindowProcW(hwnd, msg, wParam, lParam);
} catch (...) {
    CrashLog(L"FloatingIcon::WndProc", "(non-std exception)");
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey
