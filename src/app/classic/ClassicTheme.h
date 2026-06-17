// VKey Classic — Native Windows Theme Engine
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "ClassicDialogUtils.h"
#include <Windows.h>
#include <dwmapi.h>
#include <cstdint>

namespace NextKey::Classic {

/// System color tokens (resolved via GetSysColor at runtime)
struct ThemeColors {
    COLORREF background;    // COLOR_WINDOW
    COLORREF surface;       // COLOR_BTNFACE
    COLORREF text;          // COLOR_WINDOWTEXT
    COLORREF textSecondary; // COLOR_GRAYTEXT
    COLORREF accent;        // COLOR_HIGHLIGHT
    COLORREF accentText;    // COLOR_HIGHLIGHTTEXT
    COLORREF border;        // COLOR_BTNSHADOW
};

/// Font handles (created once per DPI change)
struct FontSet {
    HFONT header;  // Segoe UI Semibold, 11pt
    HFONT body;    // Segoe UI, 10pt, Regular
    HFONT bodyBold; // Segoe UI Semibold, 10pt
    HFONT entry;   // Segoe UI, 11pt, Regular (Modern entries)
};

/// Native Windows theme manager for Win32
class ClassicTheme {
public:
    ClassicTheme() = default;
    ~ClassicTheme();

    // Non-copyable
    ClassicTheme(const ClassicTheme&) = delete;
    ClassicTheme& operator=(const ClassicTheme&) = delete;

    /// Initialize theme (detect dark mode, create fonts/brushes)
    void Init(HWND hwnd, bool forceLightTheme = false);

    /// Clean up GDI resources
    void Destroy();

    /// Handle WM_SETTINGCHANGE to detect theme switch
    /// Returns true if theme actually changed (caller should InvalidateRect)
    bool OnSettingChange(LPARAM lParam);

    // -- WM_CTLCOLOR handlers (return HBRUSH) --
    HBRUSH OnCtlColorDlg(HDC hdc);
    HBRUSH OnCtlColorStatic(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorBtn(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorEdit(HDC hdc, HWND hCtrl);
    HBRUSH OnCtlColorListBox(HDC hdc, HWND hCtrl);

    /// Draw tab control item (TCS_OWNERDRAWFIXED)
    void DrawTabItem(DRAWITEMSTRUCT* dis);

    /// Draw custom checkbox (called from subclass proc)
    void DrawCheckbox(HWND hWnd, HDC hdc);

    /// Draw custom edit border (called from subclass proc)
    void DrawEditBorder(HDC hdc, const RECT& rc, bool isFocused);

    /// Draw rounded border around a hotkey edit control (used by SettingsDialog + ConvertToolDialog)
    void DrawHotkeyEditBorder(HDC hdc, HWND hwndParent, HWND hwndEdit, int controlHeight);

    /// Modern entry controls: rounded border, larger height, centered text
    int ModernHeight() const;
    void ApplyModernEntryStyle(HWND hwndCtrl);

    /// Draw 1px horizontal divider
    void DrawDivider(HDC hdc, int x, int y, int width);

    /// Apply dark mode to window caption (DWM)
    void ApplyWindowAttributes(HWND hwnd);

    /// Apply dark mode theme to all child controls (combobox, checkbox, etc.)
    void ThemeAllChildren(HWND parent);

    /// Apply dark mode to a single child control
    void ThemeChildControl(HWND hwndCtrl);

    // -- Accessors --
    [[nodiscard]] bool IsDark() const noexcept { return isDark_; }
    [[nodiscard]] const ThemeColors& Colors() const noexcept { return colors_; }
    [[nodiscard]] const FontSet& Fonts() const noexcept { return fonts_; }
    [[nodiscard]] HBRUSH BrushBackground() const noexcept { return brBackground_; }
    [[nodiscard]] UINT Dpi() const noexcept { return dpi_; }
    [[nodiscard]] int CornerRadius() const noexcept { return DpiScale(12, dpi_); }

private:
    void DetectDarkMode();
    void RefreshColors();
    void CreateBrushes();
    void DestroyBrushes();
    void CreateFonts(UINT dpi);
    void DestroyFonts();

    bool isDark_ = false;
    bool forceLightTheme_ = false;
    UINT dpi_ = 96;
    ThemeColors colors_{};
    FontSet fonts_{};

    // Cached GDI brushes
    HBRUSH brBackground_ = nullptr;
    HBRUSH brSurface_ = nullptr;

    HWND hwnd_ = nullptr;
};

} // namespace NextKey::Classic
