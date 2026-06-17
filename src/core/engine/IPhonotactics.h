// VKey - Vietnamese Phonotactics Interface
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// IPhonotactics — pure rule engine for Vietnamese syllable phonotactics.
// Encodes RuleTiengViet rules: N1/N2/N3 vowel-coda groups, tone position,
// classic vs modern orthography, c/k/qu onset agreement, closed vs pending vowels.
//
// Operates on rendered Vietnamese text (wstring_view of composed characters
// like L"ươ", L"oai") rather than internal CharState — keeps this layer
// independent of TypingEngine internals so it can be reused by other engines
// and tested in isolation.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace NextKey {
namespace Phonology {

/// Vietnamese tone marks. Values match NextKey::Tone semantically; redeclared
/// in this namespace to keep IPhonotactics independent of TypingEngine.h.
enum class Tone : uint8_t {
    None = 0,
    Acute,   // sắc
    Grave,   // huyền
    Hook,    // hỏi
    Tilde,   // ngã
    Dot      // nặng
};

/// Pure rule engine for Vietnamese syllable phonotactics.
class IPhonotactics {
public:
    virtual ~IPhonotactics() = default;

    /// Returns the index in `vowelSeq` where the tone diacritic should be placed.
    /// `vowelSeq` is the lowercase rendered Vietnamese vowel nucleus (1-3 chars,
    /// already including any modifier — e.g. L"ươ", L"oai", L"uyê").
    /// `coda` is the lowercase rendered Vietnamese coda (0-2 chars: c, m, n, p, t,
    /// nh, ng, ch). Empty wstring_view if no coda.
    /// `modernOrtho` selects modern (hoà) vs classic (hòa) tone placement for
    /// rising diphthongs oa/oe/uy.
    /// Returns SIZE_MAX if vowelSeq has no valid tone target (e.g. empty).
    [[nodiscard]] virtual size_t TonePosition(
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        bool modernOrtho) const noexcept = 0;

    /// Returns true if (onset + vowelSeq + coda + tone) forms a valid Vietnamese
    /// syllable per RuleTiengViet:
    ///  - c/k/qu onset agreement with following vowel,
    ///  - g/gh, ng/ngh agreement with i/e,
    ///  - N1/N2/N3 vowel-coda compatibility,
    ///  - 28 closed vowels reject any coda,
    ///  - 10 pending vowels require a coda,
    ///  - c, ch, p, t coda → tone restricted to Acute (sắc) or Dot (nặng).
    /// `onset` may be empty (vowel-initial syllable).
    /// `coda` may be empty.
    [[nodiscard]] virtual bool IsValidSyllable(
        std::wstring_view onset,
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        Tone tone,
        bool modernOrtho) const noexcept = 0;

    /// Returns true if `partial` is itself a valid Vietnamese syllable OR a valid
    /// prefix that can still be extended into one. Used for auto-exclusion: when
    /// a candidate modifier/key would produce a `partial` for which CanComplete
    /// returns false, the engine should reject the modification and keep the
    /// literal keystroke.
    /// `partial` is rendered Vietnamese text (composed chars), lowercase.
    [[nodiscard]] virtual bool CanComplete(
        std::wstring_view partial) const noexcept = 0;
};

}  // namespace Phonology
}  // namespace NextKey
