// VKey - Auto-Capitalization Keystroke State Machine
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Pure transition function: given the current state and a key event, returns
// the next state. Used by HookEngine::HandlePreDispatch (step 3a) when there
// is no TSF anchor truth (keystroke-based fallback). The complementary
// document-context decision is in core/AutoCapDecision.h.
//
// Extracted from HookEngine.cpp for Linux GTest coverage (HookEngine.cpp is
// Win32-only and not linked into the cross-platform VKeyTests target). The
// transition rule is the source of truth — HookEngine just dispatches and
// stores the result.

#pragma once

#include <cstdint>

namespace NextKey {

/// Keystroke-based auto-cap state machine. `HookEngine::autoCapState_` stores
/// values of this type directly (see `src/app/system/HookEngine.h`).
enum class AutoCapState : uint8_t {
    Idle = 0,            // normal typing
    AfterPunct,          // just saw . ? !
    ReadyToCapitalize,   // saw punct + space/Enter → next A-Z is the sentence start
};

namespace detail {
// VK constants used by the transition rule. Mirrored as `constexpr` so the
// header has no `<windows.h>` dependency (Linux GTest compiles this file).
inline constexpr uint32_t kVkReturn      = 0x0D;
inline constexpr uint32_t kVkSpace       = 0x20;
inline constexpr uint32_t kVkOemPeriod   = 0xBE;  // '.'
inline constexpr uint32_t kVkOemSlash    = 0xBF;  // '/' + Shift = '?'
inline constexpr uint32_t kVk1           = 0x31;  // '1' + Shift = '!'
inline constexpr uint32_t kVkAUpper      = 0x41;
inline constexpr uint32_t kVkZUpper      = 0x5A;
}  // namespace detail

/// Returns the next `AutoCapState` given the previous state and a key event.
///
/// Modifier-held shortcuts (Ctrl / Alt / Win) are passed through unchanged —
/// they are not sentence terminators (Ctrl+Enter submits forms, Ctrl+.
/// focuses Edge address bar, Win+. opens emoji picker), and the dispatcher
/// rejects them at HookEngine.cpp step 5 anyway. Without this gate, a
/// `Ctrl+Enter` would arm `ReadyToCapitalize` and the next plain letter would
/// be wrongly uppercased.
///
/// Letter keys (A-Z) are preserved — `HandleAlphaKey` consumes the state
/// when it processes the alpha key, so the transition leaves it intact.
/// All other non-sentence keys reset to `Idle`.
[[nodiscard]] constexpr AutoCapState
ComputeAutoCapStateTransition(AutoCapState current,
                              uint32_t vkCode,
                              bool shift,
                              bool ctrl,
                              bool alt,
                              bool win) noexcept {
    // Any modifier held → key is a shortcut, not sentence content.
    if (ctrl || alt || win) {
        return current;
    }

    // '.', Shift+'/', Shift+'1' → punctuation candidate.
    if (vkCode == detail::kVkOemPeriod ||
        (vkCode == detail::kVkOemSlash && shift) ||
        (vkCode == detail::kVk1 && shift)) {
        return AutoCapState::AfterPunct;
    }

    // Space after punct/ready → arm. Stays armed across multiple spaces so
    // ". ␣ ␣ c" still caps the 'c'.
    if (vkCode == detail::kVkSpace &&
        (current == AutoCapState::AfterPunct ||
         current == AutoCapState::ReadyToCapitalize)) {
        return AutoCapState::ReadyToCapitalize;
    }

    // Enter (plain) → new line, arm. Modifier-held Enter was handled above.
    if (vkCode == detail::kVkReturn) {
        return AutoCapState::ReadyToCapitalize;
    }

    // Letter key — preserve ONLY ReadyToCapitalize so HandleAlphaKey can
    // consume the pending arm. A letter following AfterPunct means the
    // punctuation was inside a token (".zip", "3.14", "a.b", "wed.day"),
    // not a sentence end — drop to Idle so a later space does NOT arm
    // capitalization of the next word.
    if (vkCode >= detail::kVkAUpper && vkCode <= detail::kVkZUpper) {
        return current == AutoCapState::ReadyToCapitalize
            ? current
            : AutoCapState::Idle;
    }

    // Any other key — not a sentence boundary, drop to Idle.
    return AutoCapState::Idle;
}

}  // namespace NextKey
