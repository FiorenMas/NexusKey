// VKey - TSF Globals Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "stdafx.h"
#include "Globals.h"
#include <initguid.h>

namespace NextKey {
namespace TSF {

// Module instance
HINSTANCE g_hInstance = nullptr;

// DLL reference count
LONG g_dllRefCount = 0;

// {DEB18BD1-2331-4F2A-B030-DA9EB0093683}
// VKey Text Service CLSID
DEFINE_GUID(CLSID_TextService,
    0xDEB18BD1, 0x2331, 0x4F2A, 0xB0, 0x30, 0xDA, 0x9E, 0xB0, 0x09, 0x36, 0x83);

// {2FE17DA4-D8E2-4B28-8566-C30E8F04BFD4}
// VKey Profile GUID (Vietnamese)
DEFINE_GUID(GUID_Profile,
    0x2FE17DA4, 0xD8E2, 0x4B28, 0x85, 0x66, 0xC3, 0x0E, 0x8F, 0x04, 0xBF, 0xD4);

// {E5B5E9F1-7A3B-4C2D-9E8F-1A2B3C4D5E6F}
// Display Attribute GUID (invisible - no underline/highlight)
const GUID GUID_DisplayAttribute_Input =
    { 0xe5b5e9f1, 0x7a3b, 0x4c2d, { 0x9e, 0x8f, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f } };

// {2C77A81E-41CC-4178-A3A7-5F8A987568E1}
// Standard TSF input mode indicator — Windows shows this in the modern input indicator tray
const GUID GUID_LangBarItem_Toggle =
    { 0x2c77a81e, 0x41cc, 0x4178, { 0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe1 } };

}  // namespace TSF
}  // namespace NextKey
