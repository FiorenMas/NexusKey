// VKey - Debug Logging Infrastructure
// SPDX-License-Identifier: AGPL-3.0-only
//
// Backed by NextKey::Logger — a runtime-gated file sink toggled by the user
// from Settings → System → "Bật debug log".
//
// Cost when toggle is OFF (the default): a single inline acquire atomic load
// + well-predicted branch. The variadic arguments are NOT evaluated, so hot-
// path call sites like HOOK_LOG inside LowLevelKeyboardProc pay near-zero
// overhead even when arguments are non-trivial expressions.
//
// In _DEBUG / NEXTKEY_DEBUG builds, Logger::Log internally fans the same
// formatted line out to OutputDebugStringW so DebugView traces keep working
// even when the file toggle is off. Args are evaluated exactly once.

#pragma once

#include "core/Logger.h"

#define NEXTKEY_LOG(fmt, ...) do {                                           \
    if (::NextKey::Logger::IsEnabled())                                      \
        ::NextKey::Logger::Log(L"[VKey] " fmt, ##__VA_ARGS__);           \
} while (0)
