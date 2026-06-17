// Tests for DecideCjkSwitch — pure CJK auto-switch state machine.
// Mirror pattern used by HookEngineAtomicTests: HookEngine.cpp is Windows-only,
// so we test the extracted decision in isolation.
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "core/CjkSwitchDecision.h"

using NextKey::CjkBeep;
using NextKey::CjkSwitchInputs;
using NextKey::CjkSwitchOutputs;
using NextKey::CjkTransition;
using NextKey::DecideCjkSwitch;

namespace {

CjkSwitchInputs MakeInputs() {
    // Default starting state: V mode in a compatible layout, no suppression,
    // not excluded, CJK auto-switch enabled. Individual tests override.
    CjkSwitchInputs in{};
    in.isCompatibleNow = true;
    in.layoutSuppressed = false;
    in.modeBeforeCjk = true;
    in.vietnameseMode = true;
    in.isExcluded = false;
    in.isForcedVietnamese = false;
    in.cjkAutoSwitchEnabled = true;
    return in;
}

}  // namespace

// ── Gating tests ──────────────────────────────────────────────────────

TEST(CjkSwitchDecision, ToggleDisabled_NoTransitionEvenOnCjkLayout) {
    auto in = MakeInputs();
    in.cjkAutoSwitchEnabled = false;
    in.isCompatibleNow = false;  // CJK layout active

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::None);
    EXPECT_FALSE(out.newLayoutSuppressed);
    EXPECT_TRUE(out.newVietnameseMode);
    EXPECT_FALSE(out.needNotifyMode);
    EXPECT_EQ(out.beep, CjkBeep::Quiet);
}

TEST(CjkSwitchDecision, ToggleDisabled_NoLeaveWhenLayoutBecomesCompatible) {
    // If user disables the toggle while already in a suppressed state,
    // DON'T spontaneously restore — that's the user's choice to manage.
    auto in = MakeInputs();
    in.cjkAutoSwitchEnabled = false;
    in.isCompatibleNow = true;
    in.layoutSuppressed = true;  // stale from before toggle disabled
    in.modeBeforeCjk = true;
    in.vietnameseMode = false;

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::None);
    EXPECT_TRUE(out.newLayoutSuppressed);   // unchanged
    EXPECT_FALSE(out.newVietnameseMode);    // unchanged
}

TEST(CjkSwitchDecision, Excluded_NoEnterEvenOnCjkLayout) {
    // Bug fix: excluded app owns the icon. CJK auto-switch must not flip
    // vietnameseMode_ while excluded, otherwise leaving-excluded can't
    // distinguish between "user pre-game state" and "CJK suppression".
    auto in = MakeInputs();
    in.isExcluded = true;
    in.isCompatibleNow = false;  // CJK layout active

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::None);
    EXPECT_FALSE(out.newLayoutSuppressed);
    EXPECT_TRUE(out.newVietnameseMode);
}

TEST(CjkSwitchDecision, Excluded_NoLeaveEvenWhenLayoutBecomesCompatible) {
    // Win+D scenario: foreground transits through Progman (en-US shell layout)
    // while still in excluded. Don't fire LeaveCjk here — wait until excluded
    // actually clears, then re-evaluate (handled by OnFocusChanged caller).
    auto in = MakeInputs();
    in.isExcluded = true;
    in.isCompatibleNow = true;
    in.layoutSuppressed = true;
    in.modeBeforeCjk = true;
    in.vietnameseMode = false;

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::None);
    EXPECT_TRUE(out.newLayoutSuppressed);   // preserved for later replay
    EXPECT_FALSE(out.newVietnameseMode);    // preserved
}

TEST(CjkSwitchDecision, ForcedVietnamese_NoEnterEvenOnCjkLayout) {
    // Per-app hard-V lock owns the mode: CJK auto-switch must not flip
    // vietnameseMode_ to E on a JA/CN/KO layout while the app is forced-V.
    auto in = MakeInputs();
    in.isForcedVietnamese = true;
    in.isCompatibleNow = false;  // CJK layout active

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::None);
    EXPECT_FALSE(out.newLayoutSuppressed);
    EXPECT_TRUE(out.newVietnameseMode);
}

TEST(CjkSwitchDecision, ForcedVietnamese_NoLeaveEvenWhenLayoutBecomesCompatible) {
    // Symmetric to the excluded gate: preserve cached suppression state for the
    // caller's leave-replay (OnFocusChanged re-evaluates when forced-V clears).
    auto in = MakeInputs();
    in.isForcedVietnamese = true;
    in.isCompatibleNow = true;
    in.layoutSuppressed = true;
    in.modeBeforeCjk = true;
    in.vietnameseMode = false;

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::None);
    EXPECT_TRUE(out.newLayoutSuppressed);
    EXPECT_FALSE(out.newVietnameseMode);
}

// ── Happy-path transitions ────────────────────────────────────────────

TEST(CjkSwitchDecision, EnterCjk_FromVMode_FlipsToE) {
    auto in = MakeInputs();
    in.isCompatibleNow = false;
    in.vietnameseMode = true;

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::EnterCjk);
    EXPECT_TRUE(out.newLayoutSuppressed);
    EXPECT_TRUE(out.newModeBeforeCjk);     // saved V state
    EXPECT_FALSE(out.newVietnameseMode);   // flipped to E
    EXPECT_TRUE(out.needNotifyMode);
    EXPECT_TRUE(out.needCommitComposition);
    EXPECT_EQ(out.beep, CjkBeep::Asterisk);
}

TEST(CjkSwitchDecision, EnterCjk_FromEMode_NoFlipNoBeep) {
    // Already in E — CJK enter still records modeBeforeCjk=false and sets
    // layoutSuppressed_, but doesn't redundantly notify or beep.
    auto in = MakeInputs();
    in.isCompatibleNow = false;
    in.vietnameseMode = false;

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::EnterCjk);
    EXPECT_TRUE(out.newLayoutSuppressed);
    EXPECT_FALSE(out.newModeBeforeCjk);    // saved E state
    EXPECT_FALSE(out.newVietnameseMode);
    EXPECT_FALSE(out.needNotifyMode);
    EXPECT_TRUE(out.needCommitComposition);
    EXPECT_EQ(out.beep, CjkBeep::Quiet);
}

TEST(CjkSwitchDecision, LeaveCjk_RestoresSavedVMode) {
    auto in = MakeInputs();
    in.isCompatibleNow = true;
    in.layoutSuppressed = true;
    in.modeBeforeCjk = true;
    in.vietnameseMode = false;  // currently suppressed

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::LeaveCjk);
    EXPECT_FALSE(out.newLayoutSuppressed);
    EXPECT_TRUE(out.newVietnameseMode);    // restored to V
    EXPECT_TRUE(out.needNotifyMode);
    EXPECT_EQ(out.beep, CjkBeep::Ok);
}

TEST(CjkSwitchDecision, LeaveCjk_PreservesSavedEMode_NoBeep) {
    // User was in E before entering CJK — restore to E (no-op for vnMode
    // but layoutSuppressed_ clears).
    auto in = MakeInputs();
    in.isCompatibleNow = true;
    in.layoutSuppressed = true;
    in.modeBeforeCjk = false;
    in.vietnameseMode = false;

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::LeaveCjk);
    EXPECT_FALSE(out.newLayoutSuppressed);
    EXPECT_FALSE(out.newVietnameseMode);
    EXPECT_TRUE(out.needNotifyMode);       // icon may need update from excluded clear
    EXPECT_EQ(out.beep, CjkBeep::Quiet);
}

// ── No-op cases ───────────────────────────────────────────────────────

TEST(CjkSwitchDecision, CompatibleLayout_NotSuppressed_NoOp) {
    auto out = DecideCjkSwitch(MakeInputs());
    EXPECT_EQ(out.transition, CjkTransition::None);
    EXPECT_FALSE(out.needNotifyMode);
}

TEST(CjkSwitchDecision, IncompatibleLayout_AlreadySuppressed_NoOp) {
    // Stayed in CJK — second focus event on same incompatible layout.
    auto in = MakeInputs();
    in.isCompatibleNow = false;
    in.layoutSuppressed = true;
    in.modeBeforeCjk = true;
    in.vietnameseMode = false;

    auto out = DecideCjkSwitch(in);

    EXPECT_EQ(out.transition, CjkTransition::None);
    EXPECT_FALSE(out.needNotifyMode);
}

// ── Integration: Win+D bug reproducer (state sequence) ────────────────

TEST(CjkSwitchDecision, WinDBug_LeavingExcludedReplaysLeaveCjk) {
    // Mirror the OnFocusChanged → OnLayoutChanged → leave-excluded re-eval
    // sequence that the caller will perform when wasExcluded becomes false.
    //
    // Sequence:
    //   1. Normal app A (V), en-US.
    //   2. Focus zh-CN excluded game → isExcluded transitions to true
    //      AFTER OnLayoutChanged runs (HookEngine ordering), so the first
    //      decision sees isExcluded=false → EnterCjk fires.
    //   3. Win+D → Progman (en-US shell), isExcluded still cached as true
    //      until OnFocusChanged finishes → first decision returns None
    //      (excluded gate). Then OnFocusChanged clears excluded and
    //      re-invokes DecideCjkSwitch with isExcluded=false and the
    //      cached compatible layout → LeaveCjk fires, restoring V.
    //
    // The decision function alone cannot enforce caller ordering, but we
    // can assert that the second eval (with isExcluded=false) does fire
    // LeaveCjk and restores vietnameseMode_.

    // Step 2: enter game. isExcluded still false at decision time.
    CjkSwitchInputs step2{};
    step2.cjkAutoSwitchEnabled = true;
    step2.isExcluded = false;          // not yet updated by caller
    step2.isCompatibleNow = false;     // game is zh-CN
    step2.layoutSuppressed = false;
    step2.modeBeforeCjk = true;
    step2.vietnameseMode = true;

    auto r2 = DecideCjkSwitch(step2);
    ASSERT_EQ(r2.transition, CjkTransition::EnterCjk);
    ASSERT_TRUE(r2.newLayoutSuppressed);
    ASSERT_FALSE(r2.newVietnameseMode);
    ASSERT_TRUE(r2.newModeBeforeCjk);

    // Step 3a: Win+D → Progman. isExcluded still true (caller hasn't
    // cleared yet), layout is now en-US.
    CjkSwitchInputs step3a{};
    step3a.cjkAutoSwitchEnabled = true;
    step3a.isExcluded = true;
    step3a.isCompatibleNow = true;
    step3a.layoutSuppressed = r2.newLayoutSuppressed;
    step3a.modeBeforeCjk    = r2.newModeBeforeCjk;
    step3a.vietnameseMode   = r2.newVietnameseMode;

    auto r3a = DecideCjkSwitch(step3a);
    // BUG FIX: excluded gate keeps layoutSuppressed/vnMode intact so the
    // replay below can still observe "was suppressed" and restore.
    ASSERT_EQ(r3a.transition, CjkTransition::None);
    ASSERT_TRUE(r3a.newLayoutSuppressed);
    ASSERT_FALSE(r3a.newVietnameseMode);

    // Step 3b: OnFocusChanged sees wasExcluded → clears isExcludedApp_
    // → re-invokes DecideCjkSwitch with the cached compatible layout.
    CjkSwitchInputs step3b = step3a;
    step3b.isExcluded = false;  // excluded just cleared

    auto r3b = DecideCjkSwitch(step3b);
    EXPECT_EQ(r3b.transition, CjkTransition::LeaveCjk);
    EXPECT_FALSE(r3b.newLayoutSuppressed);
    EXPECT_TRUE(r3b.newVietnameseMode);   // restored to V — bug fixed
    EXPECT_TRUE(r3b.needNotifyMode);
    EXPECT_EQ(r3b.beep, CjkBeep::Ok);
}
