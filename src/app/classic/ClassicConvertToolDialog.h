// VKey Classic — Convert Tool Dialog
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "ClassicDialogUtils.h"
#include "core/config/TypingConfig.h"
#include <Windows.h>
#include <commctrl.h>

namespace NextKey::Classic {

/// Win32 native dialog for text conversion tool settings.
/// Configures: case conversion toggles, encoding dropdowns, hotkey, and execute.
class ClassicConvertToolDialog {
public:
    static bool Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme = false);

private:
    ClassicConvertToolDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme);
    void CreateControls();
    void PopulateFromConfig();
    void ReadToConfig();
    void SaveConfig();
    void RefreshHotkeyDisplay();
    void DoConvert();
    void BrowseFile(bool isSource);
    void UpdateFileMode();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    int Dpi(int value) const noexcept;

    static constexpr int kWidth = 400;
    static constexpr int kHeight = 540;
    static constexpr int kPadding = 12;
    static constexpr int kRowH = 24;
    static constexpr int kRowGap = 4;
    static constexpr int kBtnHeight = 28;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;
    bool modified_ = false;

    // Case conversion toggles
    HWND checkAllCaps_ = nullptr;
    HWND checkAllLower_ = nullptr;
    HWND checkCapsFirst_ = nullptr;
    HWND checkCapsEach_ = nullptr;
    HWND checkRemoveMark_ = nullptr;

    // Options
    HWND checkAlertDone_ = nullptr;
    HWND checkAutoPaste_ = nullptr;
    HWND checkSequential_ = nullptr;
    HWND checkEnableLog_ = nullptr;

    // Encoding
    HWND labelEncodingSource_ = nullptr;
    HWND comboSource_ = nullptr;
    HWND labelEncodingDest_ = nullptr;
    HWND comboDest_ = nullptr;

    // Hotkey
    HWND labelHotkey_      = nullptr;
    HWND btnRecordHotkey_  = nullptr;  // shows current binding + opens capture modal
    HWND btnClearHotkey_   = nullptr;  // clears binding (disabled when no hotkey set)

    // Source mode (clipboard / file)
    HWND radioClipboard_ = nullptr;
    HWND radioFile_ = nullptr;
    HWND labelSourceFile_ = nullptr;
    HWND editSourcePath_ = nullptr;
    HWND btnBrowseSource_ = nullptr;
    HWND labelDestFile_ = nullptr;
    HWND editDestPath_ = nullptr;
    HWND btnBrowseDest_ = nullptr;
    bool fileMode_ = false;

    // Buttons
    HWND btnConvert_ = nullptr;
    HWND btnClose_ = nullptr;

    ConvertConfig config_{};

    static constexpr const wchar_t* kClassName = L"VKeyConvertTool";
};

}  // namespace NextKey::Classic

#endif
