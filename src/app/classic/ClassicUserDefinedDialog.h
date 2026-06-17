// VKey Classic — User Defined Input Dialog Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "ClassicDialogUtils.h"
#include "core/engine/TypingAction.h"
#include <Windows.h>
#include <commctrl.h>
#include <string>
#include <array>

namespace NextKey::Classic {

/// Win32 native dialog for editing user-defined input method keymap.
class ClassicUserDefinedDialog {
public:
    /// Show modal dialog. Returns true if keymap was modified.
    static bool Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme = false);

private:
    ClassicUserDefinedDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme);
    void CreateControls();
    void PopulateList();
    void ApplyAction();        // Assign typed key to selected action (clears old key)
    void ClearSelectedAction();  // Clear the key for the currently-selected action
    void OnActionChanged();    // Combo selection → populate edit with current key
    void OnListSelectionChanged();  // ListView selection → set combo + edit
    void LoadTemplate(bool telex);
    void ImportFromFile();
    void ExportToFile();

    /// Reassign action to newKey, clearing any other key holding same action.
    void ApplyActionToMap(TypingAction action, wchar_t newKey) noexcept;
    void ClearActionFromMap(TypingAction action) noexcept;

    void LoadData();
    void SaveData();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    int Dpi(int value) const noexcept;

    static constexpr int kWidth = 380;
    static constexpr int kHeight = 356; // Adjusted to match control placement
    static constexpr int kPadding = 12;
    static constexpr int kBtnHeight = 28;
    static constexpr int kBtnGap = 6;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;
    bool modified_ = false;

    HWND listView_ = nullptr;
    HWND editKey_ = nullptr;
    HWND comboAction_ = nullptr;
    HWND btnApply_ = nullptr;
    HWND btnClear_ = nullptr;
    HWND btnLoadTelex_ = nullptr;
    HWND btnLoadVni_ = nullptr;
    HWND btnImport_ = nullptr;
    HWND btnExport_ = nullptr;

    std::array<TypingAction, 128> keyMap_;

    static constexpr const wchar_t* kClassName = L"VKeyUserDefinedTable";
};

}  // namespace NextKey::Classic

#endif
