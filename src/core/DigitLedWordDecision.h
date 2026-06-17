// VKey - Pure decision function for "digit-led word" detection
// SPDX-License-Identifier: AGPL-3.0-only
//
// A digit-led word is a word that starts with a digit while the input method
// is VNI / Combined / UserDefined. The entire word — up to the next word
// boundary — is treated as English (passed through verbatim, no engine
// processing). Rationale: VNI digits double as tone/modifier keys, but a
// number-led token ("747 plane", "6 dollars") is never Vietnamese.
//
// Extracted from HookEngine + TSF EngineController so the state-machine
// logic can be unit-tested on Linux (both call sites are Windows-only).
// Caller owns the `currentlyArmed` bool; this function only decides what
// the next transition is, given (key, mode, engine state, current arm flag).
//
// Reset rule (boundary key):
//   - Whitespace / control: Space, Enter, Tab, Escape
//   - Editing:              Backspace, Delete
//   - Navigation:           arrows, Home, End, PgUp, PgDn
// Punctuation (`.` `,` `-` `+` …) does NOT reset — "6.5" stays one word.
//
// Telex / SimpleTelex are NOT covered: digits in those modes already pass
// through naturally (no tone/modifier meaning), so digit-led arming would
// only break the legitimate "6abc → 6+composition(abc)" use case.

#pragma once

#include <cstdint>

#include "core/config/TypingConfig.h"  // InputMethod

namespace NextKey {

/// Windows VK constants re-declared so this header has no <windows.h>
/// dependency and Linux unit tests can call DecideDigitLed directly.
namespace DigitLedVk {
    constexpr uint32_t kBack   = 0x08;
    constexpr uint32_t kTab    = 0x09;
    constexpr uint32_t kReturn = 0x0D;
    constexpr uint32_t kEscape = 0x1B;
    constexpr uint32_t kSpace  = 0x20;
    constexpr uint32_t kPrior  = 0x21;
    constexpr uint32_t kNext   = 0x22;
    constexpr uint32_t kEnd    = 0x23;
    constexpr uint32_t kHome   = 0x24;
    constexpr uint32_t kLeft   = 0x25;
    constexpr uint32_t kUp     = 0x26;
    constexpr uint32_t kRight  = 0x27;
    constexpr uint32_t kDown   = 0x28;
    constexpr uint32_t kDelete = 0x2E;
    constexpr uint32_t kDigit0 = 0x30;
    constexpr uint32_t kDigit9 = 0x39;
}

enum class DigitLedDecision : uint8_t {
    Continue,  // Not digit-led — caller proceeds with normal dispatch.
    Arm,       // Caller sets `currentlyArmed = true` and passes key through.
    Bypass,    // Currently armed, non-boundary key — caller passes key through.
    Reset,     // Currently armed, boundary key — caller clears arm flag and passes key through.
};

struct DigitLedInputs {
    uint32_t vkCode;
    bool shift;            // GetKeyState(VK_SHIFT) high bit
    bool engineEmpty;      // engine_->Count() == 0
    InputMethod method;
    bool currentlyArmed;   // value of caller's digit-led flag at entry
};

/// Word-boundary check for the digit-led run. Public so callers that
/// coordinate with other flags (e.g. HookEngine's tempEngineOff_ reset)
/// can query the rule without going through the full decision function.
[[nodiscard]] constexpr bool IsDigitLedBoundary(uint32_t vkCode) noexcept {
    if (vkCode == DigitLedVk::kSpace  || vkCode == DigitLedVk::kReturn ||
        vkCode == DigitLedVk::kTab    || vkCode == DigitLedVk::kEscape) return true;
    if (vkCode == DigitLedVk::kBack   || vkCode == DigitLedVk::kDelete) return true;
    if (vkCode >= DigitLedVk::kLeft && vkCode <= DigitLedVk::kDown) return true;
    if (vkCode == DigitLedVk::kHome  || vkCode == DigitLedVk::kEnd ||
        vkCode == DigitLedVk::kPrior || vkCode == DigitLedVk::kNext) return true;
    return false;
}

/// Pure: no I/O, no syscalls, no static state. Caller owns the arm flag and
/// applies the transition described by the return value. Safe to call from
/// any thread.
[[nodiscard]] constexpr DigitLedDecision DecideDigitLed(const DigitLedInputs& in) noexcept {
    if (in.currentlyArmed) {
        return IsDigitLedBoundary(in.vkCode) ? DigitLedDecision::Reset
                                              : DigitLedDecision::Bypass;
    }
    if (in.shift) return DigitLedDecision::Continue;
    if (!in.engineEmpty) return DigitLedDecision::Continue;
    if (in.vkCode < DigitLedVk::kDigit0 || in.vkCode > DigitLedVk::kDigit9) {
        return DigitLedDecision::Continue;
    }
    if (in.method != InputMethod::VNI &&
        in.method != InputMethod::Combined &&
        in.method != InputMethod::UserDefined) {
        return DigitLedDecision::Continue;
    }
    return DigitLedDecision::Arm;
}

}  // namespace NextKey
