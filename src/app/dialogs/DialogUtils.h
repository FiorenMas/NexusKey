// VKey - Shared file dialog helpers
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <commdlg.h>
#include <string>
#include <cwchar>

namespace NextKey {

/// Shows an Open File dialog. Returns selected path, or empty string if cancelled.
inline std::wstring ShowOpenFileDialogW(HWND hwnd, const wchar_t* filter, const wchar_t* defExt) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? buf : L"";
}

/// Shows a Save File dialog. Returns selected path, or empty string if cancelled.
/// @param defaultName  Suggested filename (no extension), e.g. L"VKeyMacro"
inline std::wstring ShowSaveFileDialogW(HWND hwnd, const wchar_t* filter,
                                         const wchar_t* defExt, const wchar_t* defaultName) {
    wchar_t buf[MAX_PATH] = {};
    if (defaultName) wcsncpy_s(buf, MAX_PATH, defaultName, _TRUNCATE);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&ofn) ? buf : L"";
}

} // namespace NextKey
