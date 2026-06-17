// VKey Classic — Macro Table Dialog
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "ClassicDialogUtils.h"
#include <Windows.h>
#include <commctrl.h>
#include <string>
#include <unordered_map>

namespace NextKey::Classic {

/// Win32 native dialog for editing macro table (key → expansion).
class ClassicMacroTableDialog {
public:
    /// Show modal dialog. Returns true if macros were modified.
    static bool Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme = false);

private:
    ClassicMacroTableDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme);
    void CreateControls();
    void PopulateList();
    void AddMacro();
    void DeleteSelected();
    void ImportFromFile();
    void ExportToFile();

    void LoadData();
    void SaveData();
    void SaveTriggerConfig();

    static std::wstring StorageToEdit(const std::wstring& s);
    static std::wstring EditToStorage(const std::wstring& s);
    std::wstring GetEditValue() const;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    int Dpi(int value) const noexcept;

    static constexpr int kWidth = 440;
    static constexpr int kHeight = 405;
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
    HWND editValue_ = nullptr;
    HWND btnAdd_ = nullptr;
    HWND btnDelete_ = nullptr;
    HWND btnImport_ = nullptr;
    HWND btnExport_ = nullptr;

    HWND lblTrigger_ = nullptr;
    HWND chkSpace_ = nullptr;
    HWND chkEnter_ = nullptr;
    HWND chkTab_ = nullptr;
    HWND chkDir_ = nullptr;

    std::unordered_map<std::wstring, std::wstring> macros_;

    static constexpr const wchar_t* kClassName = L"VKeyMacroTable";
};

}  // namespace NextKey::Classic

#endif
