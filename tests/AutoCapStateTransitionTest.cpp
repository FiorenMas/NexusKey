// VKey — ComputeAutoCapStateTransition unit tests (Linux-portable)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pinpoints the regression first surfaced in VKey_VKey_3404.log:
// Ctrl+Enter armed ReadyToCapitalize → next plain 'c' was uppercased.
// The bug lived in HookEngine.cpp step 3a; the fix moved the rule into
// core/AutoCapStateTransition.h and gates the whole transition by
// `!ctrl && !alt && !win`. These tests pin the contract so future
// re-extractions cannot regress the modifier gate silently.

#include <gtest/gtest.h>

#include "core/AutoCapStateTransition.h"

namespace NextKey {
namespace {

constexpr uint32_t kVkA          = 0x41;
constexpr uint32_t kVkC          = 0x43;
constexpr uint32_t kVkI          = 0x49;
constexpr uint32_t kVkN          = 0x4E;
constexpr uint32_t kVkP          = 0x50;
constexpr uint32_t kVkV          = 0x56;
constexpr uint32_t kVkZ          = 0x5A;
constexpr uint32_t kVkReturn     = 0x0D;
constexpr uint32_t kVkSpace      = 0x20;
constexpr uint32_t kVkOemPeriod  = 0xBE;
constexpr uint32_t kVkOemSlash   = 0xBF;
constexpr uint32_t kVk1          = 0x31;
constexpr uint32_t kVkBack       = 0x08;
constexpr uint32_t kVkLeft       = 0x25;

// Shorthand: no modifiers.
constexpr AutoCapState Step(AutoCapState s, uint32_t vk) {
    return ComputeAutoCapStateTransition(s, vk, /*shift*/ false,
                                         /*ctrl*/ false, /*alt*/ false, /*win*/ false);
}

// =====================================================================
// Baseline sentence-end transitions (regression-protect existing behavior).
// =====================================================================

TEST(AutoCapStateTransition, DotThenSpaceArmsCap) {
    auto s = Step(AutoCapState::Idle, kVkOemPeriod);
    EXPECT_EQ(s, AutoCapState::AfterPunct);
    s = Step(s, kVkSpace);
    EXPECT_EQ(s, AutoCapState::ReadyToCapitalize);
}

TEST(AutoCapStateTransition, MultipleSpacesPreserveArm) {
    auto s = AutoCapState::ReadyToCapitalize;
    s = Step(s, kVkSpace);
    EXPECT_EQ(s, AutoCapState::ReadyToCapitalize);
    s = Step(s, kVkSpace);
    EXPECT_EQ(s, AutoCapState::ReadyToCapitalize);
}

TEST(AutoCapStateTransition, PlainEnterArmsCap) {
    EXPECT_EQ(Step(AutoCapState::Idle, kVkReturn), AutoCapState::ReadyToCapitalize);
}

TEST(AutoCapStateTransition, ShiftSlashIsQuestionMark) {
    auto s = ComputeAutoCapStateTransition(AutoCapState::Idle, kVkOemSlash,
                                           /*shift*/ true, false, false, false);
    EXPECT_EQ(s, AutoCapState::AfterPunct);
}

TEST(AutoCapStateTransition, ShiftOneIsBang) {
    auto s = ComputeAutoCapStateTransition(AutoCapState::Idle, kVk1,
                                           /*shift*/ true, false, false, false);
    EXPECT_EQ(s, AutoCapState::AfterPunct);
}

TEST(AutoCapStateTransition, SlashWithoutShiftIsNotPunct) {
    auto s = ComputeAutoCapStateTransition(AutoCapState::Idle, kVkOemSlash,
                                           /*shift*/ false, false, false, false);
    EXPECT_EQ(s, AutoCapState::Idle);
}

TEST(AutoCapStateTransition, LetterKeyPreservesReadyToCapitalize) {
    // ReadyToCapitalize must survive the letter event itself —
    // HandleAlphaKey consumes the arm after the transition fires.
    EXPECT_EQ(Step(AutoCapState::ReadyToCapitalize, kVkC),
              AutoCapState::ReadyToCapitalize);
}

TEST(AutoCapStateTransition, LetterAfterPunctDropsToIdle) {
    // Letter directly after '.' means the dot is inside a token
    // (".zip", "3.14", "a.b"), not a sentence end.
    EXPECT_EQ(Step(AutoCapState::AfterPunct, kVkA), AutoCapState::Idle);
}

TEST(AutoCapStateTransition, NonSentenceKeyDropsToIdle) {
    EXPECT_EQ(Step(AutoCapState::ReadyToCapitalize, kVkBack), AutoCapState::Idle);
    EXPECT_EQ(Step(AutoCapState::AfterPunct, kVkLeft),       AutoCapState::Idle);
}

// =====================================================================
// Regression: VKey_VKey_3404.log
// Ctrl+Enter must NOT arm. Then plain 'c' must stay Idle.
// =====================================================================

TEST(AutoCapStateTransition, CtrlEnterDoesNotArmCap) {
    auto s = ComputeAutoCapStateTransition(AutoCapState::Idle, kVkReturn,
                                           /*shift*/ false, /*ctrl*/ true,
                                           /*alt*/ false, /*win*/ false);
    EXPECT_EQ(s, AutoCapState::Idle)
        << "Ctrl+Enter is a shortcut (form submit / new tab), not a sentence end";
}

TEST(AutoCapStateTransition, CtrlDotDoesNotArm) {
    auto s = ComputeAutoCapStateTransition(AutoCapState::Idle, kVkOemPeriod,
                                           /*shift*/ false, /*ctrl*/ true,
                                           /*alt*/ false, /*win*/ false);
    EXPECT_EQ(s, AutoCapState::Idle)
        << "Ctrl+. is Edge's focus-address-bar shortcut, not a sentence end";
}

TEST(AutoCapStateTransition, WinDotDoesNotArm) {
    auto s = ComputeAutoCapStateTransition(AutoCapState::Idle, kVkOemPeriod,
                                           /*shift*/ false, /*ctrl*/ false,
                                           /*alt*/ false, /*win*/ true);
    EXPECT_EQ(s, AutoCapState::Idle)
        << "Win+. opens the emoji picker, not a sentence end";
}

TEST(AutoCapStateTransition, AltEnterDoesNotArm) {
    auto s = ComputeAutoCapStateTransition(AutoCapState::Idle, kVkReturn,
                                           false, false, /*alt*/ true, false);
    EXPECT_EQ(s, AutoCapState::Idle);
}

// Full Ctrl+Enter → Ctrl+V → 'c' sequence from the bug report. After the
// Ctrl-held keys leave state at Idle, the plain 'c' must NOT find the
// machine in ReadyToCapitalize.
TEST(AutoCapStateTransition, BugReport_CtrlEnter_then_plain_c_stays_Idle) {
    auto s = AutoCapState::Idle;
    // Ctrl+Enter
    s = ComputeAutoCapStateTransition(s, kVkReturn, false, true, false, false);
    EXPECT_EQ(s, AutoCapState::Idle);
    // Ctrl+V (letter key with Ctrl)
    s = ComputeAutoCapStateTransition(s, kVkV, false, true, false, false);
    EXPECT_EQ(s, AutoCapState::Idle);
    // Plain 'c'
    s = Step(s, kVkC);
    EXPECT_EQ(s, AutoCapState::Idle)
        << "Without the modifier gate, this would be ReadyToCapitalize and "
           "HandleAlphaKey would uppercase 'c' to 'C' — the original bug.";
}

// Counter-test: the same sequence WITHOUT modifiers must still arm, so the
// gate is not over-broad (only blocks shortcuts, not regular typing).
TEST(AutoCapStateTransition, BugReport_PlainEnter_then_c_still_arms) {
    auto s = AutoCapState::Idle;
    s = Step(s, kVkReturn);
    EXPECT_EQ(s, AutoCapState::ReadyToCapitalize);
    s = Step(s, kVkC);
    EXPECT_EQ(s, AutoCapState::ReadyToCapitalize)
        << "Plain Enter then letter must keep arm so HandleAlphaKey can cap";
}

TEST(AutoCapStateTransition, BugReport_DotSpace_then_c_still_arms) {
    auto s = AutoCapState::Idle;
    s = Step(s, kVkOemPeriod);
    s = Step(s, kVkSpace);
    EXPECT_EQ(s, AutoCapState::ReadyToCapitalize);
    s = Step(s, kVkC);
    EXPECT_EQ(s, AutoCapState::ReadyToCapitalize);
}

// Regression pin: ".zip" + space + word must NOT capitalize the word.
// Confirms the full sequence (not just the single-step transition) so
// a future change to either the AfterPunct→letter or AfterPunct→space
// rule can't reintroduce the bug.
TEST(AutoCapStateTransition, BugReport_DotZipSpace_doesNotArm) {
    auto s = AutoCapState::Idle;
    s = Step(s, kVkOemPeriod);
    EXPECT_EQ(s, AutoCapState::AfterPunct);
    s = Step(s, kVkZ);
    EXPECT_EQ(s, AutoCapState::Idle);
    s = Step(s, kVkI);
    s = Step(s, kVkP);
    s = Step(s, kVkSpace);
    EXPECT_EQ(s, AutoCapState::Idle);
    s = Step(s, kVkN);
    EXPECT_EQ(s, AutoCapState::Idle);
}

}  // namespace
}  // namespace NextKey
