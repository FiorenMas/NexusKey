// Tests for DecidePerAppMode — pure per-app mode-lock resolution.
// HookEngine/FocusOwner are Windows-only; the decision is extracted so the
// precedence rule is verified in isolation on Linux.
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "core/PerAppModeDecision.h"

using NextKey::DecidePerAppMode;
using NextKey::PerAppMode;

TEST(PerAppModeDecision, NeitherSet_None) {
    EXPECT_EQ(DecidePerAppMode(/*excluded*/ false, /*forcedVn*/ false),
              PerAppMode::None);
}

TEST(PerAppModeDecision, ExcludedOnly_ForceEnglish) {
    EXPECT_EQ(DecidePerAppMode(/*excluded*/ true, /*forcedVn*/ false),
              PerAppMode::ForceEnglish);
}

TEST(PerAppModeDecision, ForcedVnOnly_ForceVietnamese) {
    EXPECT_EQ(DecidePerAppMode(/*excluded*/ false, /*forcedVn*/ true),
              PerAppMode::ForceVietnamese);
}

TEST(PerAppModeDecision, BothSet_ExcludedWins) {
    // Corrupted / hand-edited config: an exe listed in BOTH sets. Excluded
    // (transparent) must win — never lock the user out of English by forcing V.
    EXPECT_EQ(DecidePerAppMode(/*excluded*/ true, /*forcedVn*/ true),
              PerAppMode::ForceEnglish);
}
