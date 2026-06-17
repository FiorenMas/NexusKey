// VKey - TSF Common Definitions
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "core/Logger.h"

// Common macros and defines for TSF module
#define VKEY_TSF_VERSION_MAJOR 1
#define VKEY_TSF_VERSION_MINOR 0
#define VKEY_TSF_VERSION_PATCH 0

// TSF_LOG → unified runtime-gated Logger.
// Enabled by Settings → System → "Bật debug log" (propagated to TSF DLL via
// SharedState feature flags during ApplySharedState).
//
// Cost when off: inline atomic load + branch, args NOT evaluated. In _DEBUG
// builds Logger::Log internally fans the same formatted line out to
// OutputDebugStringW so DebugView traces continue to work.
#define TSF_LOG(fmt, ...) do {                                               \
    if (::NextKey::Logger::IsEnabled())                                      \
        ::NextKey::Logger::Log(L"[TSF] " fmt, ##__VA_ARGS__);                \
} while (0)
