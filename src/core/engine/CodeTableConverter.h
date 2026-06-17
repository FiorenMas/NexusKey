// VKey - Code Table Converter
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
// Dual-licensed: AGPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
//
// Converts Unicode Vietnamese characters to legacy encodings
// (TCVN3, VNI Windows, Unicode Compound, CP 1258).

#pragma once

#include "core/config/TypingConfig.h"
#include <cstdint>
#include <string>

namespace NextKey {

/// Result of encoding a single Unicode character.
/// For single-byte encodings (TCVN3), count=1.
/// For multi-unit encodings (VNI, Compound, CP1258), count may be 1 or 2.
struct EncodedChar {
    wchar_t units[2] = {0, 0};  // units[0] = primary, units[1] = secondary (0 if none)
    uint8_t count = 1;           // 1 or 2 output units
};

namespace CodeTableConverter {

/// Convert a single Unicode character to its encoded form for the given code table.
/// For CodeTable::Unicode, returns the character as-is (1 unit, zero overhead).
/// For non-Vietnamese characters (ASCII, spaces, digits), returns as-is in all tables.
[[nodiscard]] EncodedChar ConvertChar(wchar_t ch, CodeTable table) noexcept;

/// Decode an encoded string back to Unicode.
[[nodiscard]] std::wstring DecodeString(const std::wstring& input, CodeTable from) noexcept;

/// Encode a Unicode string to the given code table.
[[nodiscard]] std::wstring EncodeString(const std::wstring& input, CodeTable to) noexcept;

/// Remove all Vietnamese diacritics (tones + modifiers), e.g. "Việt" → "Viet".
[[nodiscard]] std::wstring RemoveDiacritics(const std::wstring& input) noexcept;

/// Vietnamese-aware uppercase conversion.
[[nodiscard]] std::wstring ToUpper(const std::wstring& input) noexcept;

/// Vietnamese-aware lowercase conversion.
[[nodiscard]] std::wstring ToLower(const std::wstring& input) noexcept;

/// Capitalize first letter of each sentence (after .!?\n), lowercasing the rest.
[[nodiscard]] std::wstring ToSentenceCase(const std::wstring& input) noexcept;

/// Capitalize first letter of each word, lowercasing the rest.
[[nodiscard]] std::wstring ToTitleCase(const std::wstring& input) noexcept;

}  // namespace CodeTableConverter

}  // namespace NextKey
