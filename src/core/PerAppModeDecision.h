// VKey - Pure decision function for the per-app mode lock (hard-E / hard-V).
// SPDX-License-Identifier: AGPL-3.0-only
//
// "Excluded apps" historically forced English by making the IME fully
// transparent (hook passthrough + TSF dormant). The per-app mode lock
// generalizes this: each listed app is locked to EITHER ForceEnglish (the
// legacy excluded/transparent behaviour) OR ForceVietnamese (force V on focus
// and block the V/E toggle while focused).
//
// Resolution is a pure function of the app's membership in the two config sets
// so it is unit-testable on Linux. FocusOwner::Classify (the Win32 caller)
// feeds the two booleans in and maps the result onto
// FocusClassification::isExcluded / ::isForcedVietnamese.
//
// Precedence: ForceEnglish (excluded) WINS over ForceVietnamese. A corrupted or
// hand-edited config that lists an exe in both sets can never double-lock —
// transparent is the safer default (the user is never locked out of English).
// ConfigSnapshotBuilder also drops any such exe from the forced-V set at build
// time so the two snapshot sets stay disjoint; this precedence is the
// belt-and-suspenders runtime guarantee.

#pragma once

#include <cstdint>

namespace NextKey {

enum class PerAppMode : std::uint8_t {
    None,             // no per-app lock — normal V/E (global flag, smart-switch, CJK apply)
    ForceEnglish,     // excluded: transparent passthrough, toggle blocked
    ForceVietnamese,  // force Vietnamese on focus, toggle blocked
};

/// Pure: resolve the per-app lock from set membership. No I/O, no syscalls.
[[nodiscard]] inline PerAppMode DecidePerAppMode(
        bool inExcludedSet, bool inForcedVietnameseSet) noexcept {
    if (inExcludedSet)         return PerAppMode::ForceEnglish;
    if (inForcedVietnameseSet) return PerAppMode::ForceVietnamese;
    return PerAppMode::None;
}

}  // namespace NextKey
