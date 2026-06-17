// VKey - Auto-Capitalization Decision
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Pure decision: given a snapshot of text up to (but not including) the
// caret, classify which "should the next char start fresh" trigger applies.
//
// Two consumers share this rule:
//   1. TSF `InspectPrecedingTextEditSession` (Win32 edit session, wchar_t
//      buffer) — collapses the trigger into a single auto-cap bool via
//      `ComputeShouldAutoCap`.
//   2. IPC anchor `DeriveAnchorFromPreceding` in `core/ipc/SharedState.h`
//      (uint16_t buffer, cross-process) — splits the trigger into the
//      `isSentenceStart` / `isLineStart` flags published to HookEngine.
//
// Linux-portable so the rule has Linux GTest coverage even though both
// consumers obtain their buffers via Windows-specific paths.

#pragma once

#include <cstddef>
#include <cstdint>

namespace NextKey {

/// Classification of the preceding-text trigger that warrants a fresh start
/// (capitalize the next letter / mark a new sentence in the IPC anchor).
enum class CapTrigger : uint8_t {
    None,         // mid-word / mid-sentence / non-sentence punct — no fresh start
    DocStart,     // empty buffer or only whitespace — both sentence + line start
    LineStart,    // last non-whitespace is `\n` or `\r` — line start, not sentence
    SentenceEnd,  // last non-whitespace is `.`, `?`, or `!` AND at least one
                  // whitespace separates it from the caret (so domains like
                  // ".com" / ".vn" do NOT trigger)
};

/// Classify the trigger by walking back over trailing spaces/tabs and
/// inspecting the first non-whitespace char. Templated so the IPC path
/// (uint16_t / UTF-16) and the TSF path (wchar_t) share one implementation.
template <typename CharT>
[[nodiscard]] constexpr CapTrigger ClassifyCapTrigger(const CharT* buf,
                                                      std::size_t len) noexcept {
    if (buf == nullptr || len == 0) return CapTrigger::DocStart;

    std::size_t i = len;
    bool skippedWhitespace = false;
    while (i > 0) {
        const CharT c = buf[i - 1];
        if (c == CharT{' '} || c == CharT{'\t'}) {
            --i;
            skippedWhitespace = true;
        } else {
            break;
        }
    }

    if (i == 0) return CapTrigger::DocStart;

    const CharT c = buf[i - 1];
    if (c == CharT{'\n'} || c == CharT{'\r'}) return CapTrigger::LineStart;
    if ((c == CharT{'.'} || c == CharT{'?'} || c == CharT{'!'}) && skippedWhitespace) {
        return CapTrigger::SentenceEnd;
    }
    return CapTrigger::None;
}

/// Convenience wrapper for the TSF auto-cap call site: any non-`None`
/// trigger means the next typed letter should be capitalized.
[[nodiscard]] constexpr bool ComputeShouldAutoCap(const wchar_t* buf,
                                                  std::size_t len) noexcept {
    return ClassifyCapTrigger(buf, len) != CapTrigger::None;
}

}  // namespace NextKey
