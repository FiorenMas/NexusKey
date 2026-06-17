// VKey - Macro expansion decision logic
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Linux-portable macro expansion decision unit. Extracted from
// HookEngine::TryExpandMacro for unit testability. Win32 case-mapping
// dependency is injected via the CaseMapper interface.

#pragma once

#include "core/config/TypingConfig.h"   // CodeTable enum

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NextKey::Macro {

/// DI seam for locale-aware uppercase / lowercase. Production wraps
/// CharUpperBuffW / CharLowerBuffW; tests use ASCII-only mappers.
class CaseMapper {
public:
    virtual ~CaseMapper() = default;
    virtual void Upper(wchar_t* buf, std::size_t n) const = 0;
    virtual void Lower(wchar_t* buf, std::size_t n) const = 0;
};

struct PlanInputs {
    const std::wstring& rawMacroBuffer;
    const std::wstring& previousComposition;
    const std::vector<uint8_t>& previousEncodedWidths;   // Only read when currentCodeTable != Unicode
    const std::unordered_map<std::wstring, std::wstring>& macroTable;
    bool macroCrossCommit;
    CodeTable currentCodeTable;
    bool autoCapsEnabled;
    wchar_t triggerChar;
    std::size_t clipboardThreshold;     // = HookEngine kMacroClipboardThreshold (200)
};

struct MacroPlan {
    bool matched{false};
    bool isPartOfMacro{false};          // → ExpandedEatTrigger vs ExpandedPassTrigger
    std::size_t bsCount{0};
    std::wstring expansion;             // post auto-caps; \n escapes still literal
    bool useClipboard{false};
};

/// Decide what (if anything) to expand. Pure function: no I/O, no globals.
[[nodiscard]] MacroPlan Plan(const PlanInputs& in, const CaseMapper& mapper);

/// Convert \n escapes into \r\n for clipboard paste. No other escapes processed.
[[nodiscard]] std::wstring ExpandEscapesForClipboard(std::wstring_view expansion);

/// Single dispatch fragment: either text or a VK_RETURN marker.
struct Segment {
    bool isReturn{false};
    std::wstring text;                  // empty when isReturn == true
};

/// Split expansion at \n boundaries and run per-character ConvertChar
/// for non-Unicode code tables. Returns segments interleaved with returns.
[[nodiscard]] std::vector<Segment> BuildSegments(std::wstring_view expansion,
                                                 CodeTable codeTable);

}  // namespace NextKey::Macro
