// VKey - TSF Global Definitions
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <Windows.h>
#include <msctf.h>
#include <string>

namespace NextKey {
namespace TSF {

// GUIDs - will be defined in Globals.cpp
extern const GUID CLSID_TextService;
extern const GUID GUID_Profile;
extern const GUID GUID_DisplayAttribute_Input;
extern const GUID GUID_LangBarItem_Toggle;

// Module instance handle
extern HINSTANCE g_hInstance;

// DLL reference count
extern LONG g_dllRefCount;

// String constants
constexpr const wchar_t* TEXT_SERVICE_DESCRIPTION = L"VKey Vietnamese IME";

// CLSID as string for registry checks (matches CLSID_TextService in Globals.cpp)
// {DEB18BD1-2331-4F2A-B030-DA9EB0093683}
constexpr const wchar_t* CLSID_TEXTSERVICE_STRING = L"{DEB18BD1-2331-4F2A-B030-DA9EB0093683}";
// Use English keyboard as base - VKey handles Vietnamese conversion via Telex
constexpr LANGID TEXTSERVICE_LANGID = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
constexpr ULONG TEXTSERVICE_ICON_INDEX = 0;

}  // namespace TSF
}  // namespace NextKey
