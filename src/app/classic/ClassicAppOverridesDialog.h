// VKey Classic — App Overrides Dialog
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "ClassicDialogUtils.h"
#include "core/config/ConfigManager.h"
#include <Windows.h>
#include <commctrl.h>
#include <string>
#include <unordered_map>

namespace NextKey::Classic {

/// Win32 native dialog for per-app encoding/method overrides.
class ClassicAppOverridesDialog {
public:
    static bool Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme = false);

private:
    ClassicAppOverridesDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme);
    void CreateControls();
    void PopulateList();
    void AddOverride();
    void DeleteSelected();
    void OnPickWindow();

    void LoadData();
    void SaveData();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    int Dpi(int value) const noexcept;

    static constexpr int kWidth = 490;
    static constexpr int kHeight = 330;
    static constexpr int kPadding = 12;
    static constexpr int kBtnHeight = 28;
    static constexpr int kBtnGap = 6;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;
    bool modified_ = false;

    HWND listView_ = nullptr;
    HWND comboApp_ = nullptr;
    HWND comboMethod_ = nullptr;
    HWND comboEncoding_ = nullptr;
    HWND comboSendMethod_ = nullptr;
    HWND btnAdd_ = nullptr;
    HWND btnPick_ = nullptr;
    HWND btnDelete_ = nullptr;

    std::unordered_map<std::wstring, AppOverrideEntry> entries_;
    WindowPicker picker_;

    static constexpr const wchar_t* kClassName = L"VKeyAppOverrides";
};

}  // namespace NextKey::Classic

#endif
