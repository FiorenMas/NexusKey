// VKey Classic — Icon Color Customization Dialog
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "ClassicDialogUtils.h"
#include <Windows.h>
#include <cstdint>

namespace NextKey::Classic {

/// Small modal dialog for customizing tray icon V/E fill colors.
/// Shows preview swatches + rendered icon letters. Click swatch to pick color.
struct IconColorResult {
    uint32_t colorV = 0;
    uint32_t colorE = 0;
    bool accepted = false;
};

class ClassicIconColorDialog {
public:
    /// Show modal dialog. initialV/E are current COLORREF values.
    static IconColorResult Show(HINSTANCE hInstance, HWND parent,
                                uint32_t initialV, uint32_t initialE,
                                bool forceLightTheme = false);

private:
    ClassicIconColorDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent, uint32_t initV, uint32_t initE,
              bool forceLightTheme);
    void CreateControls();
    void PickColor(bool isV);
    void PaintPreview(HDC hdc);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    int Dpi(int value) const noexcept;

    static constexpr int kWidth = 180;
    static constexpr int kHeight = 110;
    static constexpr int kPadding = 12;
    static constexpr int kSwatchSize = 28;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;

    HWND btnSwatchV_ = nullptr;
    HWND btnSwatchE_ = nullptr;

    COLORREF colorV_ = RGB(233, 30, 99);
    COLORREF colorE_ = RGB(33, 150, 243);
    bool colorPicked_ = false;  // True if at least one ChooseColor was accepted
    HFONT previewFont_ = nullptr;  // Cached font for icon preview painting

    static COLORREF customColors_[16];
    static constexpr const wchar_t* kClassName = L"VKeyIconColor";
};

}  // namespace NextKey::Classic

#endif
