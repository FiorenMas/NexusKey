// VKey - TSF Precompiled Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// Windows headers
#include <Windows.h>
#include <msctf.h>
#include <olectl.h>

// ATL — only CComPtr is needed (smart pointer for COM interfaces).
// Prereq: VS2022 "Desktop development with C++" + "C++ ATL" component (see CLAUDE.md).
#include <atlbase.h>

// Standard C++ headers used throughout TSF
#include <string>
#include <memory>
#include <cstdint>

// NextKey namespace forward declarations
namespace NextKey {
class SharedStateManager;
struct TypingConfig;
}
