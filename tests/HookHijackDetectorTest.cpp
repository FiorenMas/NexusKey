// HookHijackDetectorTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Anti-Dorion v2 (2026-05-28). Locks the contract that the refactored
// HookHijackDetector must satisfy. Written BEFORE the refactor per
// PHILOSOPHY.md §2 (test-first) — the existing v1 detector owned its own
// std::thread and called GetKeyboardState directly, neither of which is
// Linux-testable. This file pins the new API:
//
//   - HookHijackDetector::Poll() — runs one cycle, no thread inside
//   - HookHijackDetector::Reset() — re-establishes baselines (chromium-fg flip)
//   - HookHijackDetector::SetChromiumClassActive(bool) — gate flag
//   - Callbacks::{readKeyboardState, translateVkToChar} — platform-mockable
//
// The detector becomes a stateless processor; cadence + threading move to
// the owner (HookEngine wires Poll into MainThreadWorker's tick handler).
//
// Test surface chosen so all platform interactions go through callbacks
// → Linux-runnable. Cooldown timing (uses GetTickCount internally) is left
// to manual smoke; everything else is deterministic state-diff math.
//
// 2026-05-28 follow-up — cumulative-drift refactor.
//   The original per-poll drift comparison missed the realistic Vietnamese-
//   typing bypass pattern (1 missed key per 40 ms poll, drift never crossed
//   tolerance=1 within a single cycle, accum never built). Tests below pin
//   the new semantics: drift accumulates across polls; an overshoot from
//   the hook catching up decays it (race absorption); pendingVks_ buffer
//   collects misses across the polls leading up to the trigger.
//
// See docs/plans/2026-05-28-anti-dorion-detector-inject-design.md §Q3.

#include <gtest/gtest.h>

#include "app/system/HookHijackDetector.h"

#include <cstdint>
#include <vector>

namespace {

// Mock VK constants — defined here so the test doesn't depend on <Windows.h>.
// Values match the Win32 VK_* constants the production code uses.
constexpr uint8_t kVkBack    = 0x08;  // VK_BACK
constexpr uint8_t kVkShift   = 0x10;  // VK_SHIFT     — modifier (filtered)
constexpr uint8_t kVkControl = 0x11;  // VK_CONTROL   — modifier (filtered)
constexpr uint8_t kVkSpace   = 0x20;  // VK_SPACE

class HookHijackDetectorTest : public ::testing::Test {
protected:
    // ── Mock state observed by the detector's callbacks ───────────────
    uint8_t  mockState_[256]   = {};
    uint64_t mockHookFireCount_ = 0;
    int      reinstallCount_   = 0;
    // Per-char injection — detector loops over pendingVks at trigger
    // and calls injectGhostChar once per VK. Receiver only advances
    // engine state (no BS+replace), so the buffer of raw user keys is
    // accepted as-is — matches the EVKey "1-2 keys visible" trade.
    std::vector<wchar_t> injectedGhosts_;

    NextKey::HookHijackDetector::Callbacks MakeCallbacks() {
        NextKey::HookHijackDetector::Callbacks cb;
        cb.readHookFireCount = [this]() -> uint64_t { return mockHookFireCount_; };
        cb.requestReinstall  = [this]() { ++reinstallCount_; };
        cb.injectGhostChar   = [this](wchar_t ch) { injectedGhosts_.push_back(ch); };
        cb.readKeyboardState = [this](uint8_t (&state)[256]) -> bool {
            for (int i = 0; i < 256; ++i) state[i] = mockState_[i];
            return true;
        };
        cb.translateVkToChar = [](uint8_t vk, const uint8_t (&)[256]) -> wchar_t {
            // Stable identity-mapping for the alpha/digit/space subset the
            // detector tracks. Production wires this to ToUnicodeEx with the
            // foreground thread's HKL.
            if (vk >= 'A' && vk <= 'Z') return static_cast<wchar_t>(vk - 'A' + 'a');
            if (vk >= '0' && vk <= '9') return static_cast<wchar_t>(vk);
            if (vk == kVkSpace) return L' ';
            return 0;
        };
        return cb;
    }

    void PressKey(uint8_t vk)   { mockState_[vk] |= 0x80; }
    void ReleaseKey(uint8_t vk) { mockState_[vk] &= ~0x80; }
};

// ── 1. Baseline: nothing happening, nothing fires ─────────────────────
TEST_F(HookHijackDetectorTest, Idle_NoReinstallNoInject) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();   // establishes baselines
    detector.Poll();   // no state change since baseline
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 0);
    EXPECT_TRUE(injectedGhosts_.empty());
}

// ── 2. Gate closed: detector does no work when no chromium-class app fg ─
TEST_F(HookHijackDetectorTest, GateClosed_NoWork) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    // Deliberately DO NOT call SetChromiumClassActive(true).
    PressKey('A');
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 0);
    EXPECT_TRUE(injectedGhosts_.empty());
}

// ── 3. Hook stayed on top — every key polled is also accounted for ────
TEST_F(HookHijackDetectorTest, HookSawAllKeys_NoDrift) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();

    PressKey('A');
    mockHookFireCount_ += 1;  // hook saw the keydown (we're on top)
    detector.Poll();

    PressKey('S');
    mockHookFireCount_ += 1;
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 0);
    EXPECT_TRUE(injectedGhosts_.empty());
}

// ── 4. Burst bypass — 2 keys missed in same poll crosses tolerance=1 ──
// With cumulative drift, 2 misses in a single poll cycle yield accum=2
// which exceeds tolerance=1 immediately. Distinguishes the burst-bypass
// path (single-poll trigger) from the sequential path (test 11).
TEST_F(HookHijackDetectorTest, TwoKeysMissed_TriggersInjectAndReinstall) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();   // baseline

    PressKey('A');
    PressKey('S');
    // mockHookFireCount_ deliberately NOT bumped — Dorion ate both keys
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 1);
    ASSERT_EQ(injectedGhosts_.size(), 2u);
    EXPECT_EQ(injectedGhosts_[0], L'a');
    EXPECT_EQ(injectedGhosts_[1], L's');
}

// ── 5. Multiple keys eaten between polls — all recovered, single reinstall ─
TEST_F(HookHijackDetectorTest, MultipleKeysMissed_AllInjectedOneReinstall) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();

    PressKey('A');
    PressKey('S');
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 1);   // one reinstall covers the burst
    ASSERT_EQ(injectedGhosts_.size(), 2u);
    // VK-order: 'A' (0x41) before 'S' (0x53)
    EXPECT_EQ(injectedGhosts_[0], L'a');
    EXPECT_EQ(injectedGhosts_[1], L's');
}

// ── 6. Modifier keys must NOT count as drift ──────────────────────────
TEST_F(HookHijackDetectorTest, ModifierKeysFiltered_NoFalsePositive) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();

    PressKey(kVkShift);
    PressKey(kVkControl);
    // No hook bump; if the detector counted modifiers it would think 2 keys
    // were missed.
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 0);
    EXPECT_TRUE(injectedGhosts_.empty());
}

// ── 6b. Ghost replay uses the per-key modifier state, not trigger-time ──
// Regression for the fix: pendingVks accumulate across polls; each must be
// translated with the modifiers live WHEN it was buffered. Shift released
// between the buffered Shift+letter and the trigger must not lowercase it.
TEST_F(HookHijackDetectorTest, GhostReplayUsesPerKeyModifierState) {
    auto cb = MakeCallbacks();
    // Case-sensitive translate: uppercase iff this key's Shift byte is set.
    cb.translateVkToChar = [](uint8_t vk, const uint8_t (&st)[256]) -> wchar_t {
        if (vk >= 'A' && vk <= 'Z') {
            const bool shift = (st[kVkShift] & 0x80) != 0;
            return static_cast<wchar_t>(shift ? vk : (vk - 'A' + 'a'));
        }
        return 0;
    };
    NextKey::HookHijackDetector detector(std::move(cb));
    detector.SetChromiumClassActive(true);
    detector.Poll();  // baseline

    // Poll 1: Shift+A, hook ate it → drift=1 (accum=1, no trigger), 'A' buffered
    // with Shift down.
    PressKey(kVkShift);
    PressKey('A');
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 0);

    // Poll 2: Shift RELEASED, B pressed, hook ate it → accum=2 > tolerance=1 →
    // trigger. 'B' buffered with Shift up.
    ReleaseKey(kVkShift);
    PressKey('B');
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 1);
    ASSERT_EQ(injectedGhosts_.size(), 2u);
    EXPECT_EQ(injectedGhosts_[0], L'A')
        << "replay must use the modifier state captured WHEN 'A' was buffered "
           "(Shift down), not the trigger-time snapshot (Shift already up)";
    EXPECT_EQ(injectedGhosts_[1], L'b');
}

// ── 6c. Leaving the chromium app latches a baseline reset for the next ──
// session (consumed on the next Poll — never written from the focus thread,
// which would race the worker's Poll). Stale drift must not carry across.
TEST_F(HookHijackDetectorTest, DeactivateLatchesBaselineResetForNextSession) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();  // baseline

    PressKey('A');    // drift=1 buffered (no hook bump), accum=1, no trigger
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 0);

    detector.SetChromiumClassActive(false);  // latches reset (no poll-state write)
    detector.SetChromiumClassActive(true);   // re-enter

    // First poll of the new session consumes the latch and re-establishes
    // baselines, dropping the carried accum/buffer. A single new key then
    // stays under tolerance → no spurious trigger from stale drift.
    PressKey('S');
    detector.Poll();
    PressKey('D');
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 0)
        << "re-entry baseline reset must drop the previous session's drift";
    EXPECT_TRUE(injectedGhosts_.empty());
}

// ── 7. Tolerance absorbs a 1-key read-order race ──────────────────────
// Cumulative-drift refactor: drift=1 accumulates as accum=1 (still ≤
// tolerance, no trigger). When the hook fetch_add lands on the next
// poll, overshoot=1 decays accum to 0 and clears the pending buffer.
// Together: a transient 1-poll race never escalates into a false trigger.
TEST_F(HookHijackDetectorTest, ToleranceAbsorbsSingleKeyRace) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();

    // Poll observed 'A' down before the hook's atomic store landed → accum=1.
    PressKey('A');
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 0);
    EXPECT_TRUE(injectedGhosts_.empty());

    // Hook catches up next cycle. No new keys pressed → polled=0,
    // hookDelta=1, overshoot=1, accum decays to 0. Pending buffer cleared.
    mockHookFireCount_ += 1;
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 0);

    // 'A' release should not retrigger; accum is 0 again, baseline clean.
    ReleaseKey('A');
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 0);
    EXPECT_TRUE(injectedGhosts_.empty());
}

// ── 8. Backspace is tracked, but mock translateVk returns 0 → no inject
// Production wires translateVkToChar to ToUnicodeEx (returns 0x08 for
// VK_BACK on US-EN layout) but the engine has its own BS path; passing BS
// through engine.PushChar would be wrong. The contract: detector skips
// inject when translateVkToChar returns 0. Reinstall still fires (the
// underlying drift signal is real). Uses 2 misses to clear tolerance=1
// (BS + alpha → drift=2 in one poll).
TEST_F(HookHijackDetectorTest, BackspaceTrackedButZeroCharSkipsInject) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();

    PressKey(kVkBack);
    PressKey('A');
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 1);
    // BS translates to 0 → skipped; 'A' translates to 'a' → injected.
    ASSERT_EQ(injectedGhosts_.size(), 1u)
        << "Expected only the alpha key to inject; BS should be filtered "
           "because the mock translateVkToChar returns 0 for it.";
    EXPECT_EQ(injectedGhosts_[0], L'a');
}

// ── 9. SetChromiumClassActive(false→true) resets baselines AND accum ───
TEST_F(HookHijackDetectorTest, ChromiumFgReentry_BaselinesReset) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();
    PressKey('A');
    PressKey('S');           // 2 keys → clear tolerance=1 → bypass
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 1);

    // Exit chromium-fg, then re-enter. Baselines AND any half-built
    // accumulated drift / pending buffer should reset — the still-down
    // 'A'/'S' are part of the new baseline, not fresh transitions.
    detector.SetChromiumClassActive(false);
    injectedGhosts_.clear();
    reinstallCount_ = 0;
    detector.SetChromiumClassActive(true);
    detector.Poll();   // re-establishes baseline with 'A'/'S' already down
    detector.Poll();   // still down, not new edges

    EXPECT_EQ(reinstallCount_, 0)
        << "Re-entry must not fire reinstall on keys already down at "
           "baseline capture (Invariant 4 — no leak across sessions).";
    EXPECT_TRUE(injectedGhosts_.empty());
}

// ── 10. Reset() is the explicit baseline re-arm entry point ───────────
TEST_F(HookHijackDetectorTest, ExplicitReset_DropsPending) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();
    PressKey('A');
    detector.Reset();   // explicit baseline re-arm
    detector.Poll();    // 'A' down at reset time → not a new edge

    EXPECT_EQ(reinstallCount_, 0);
    EXPECT_TRUE(injectedGhosts_.empty());
}

// ── 11. CRITICAL — sequential single-key drift accumulates and triggers ─
// User-reported Dorion bug (2026-05-28): typing Vietnamese in Dorion with
// the hook bypassed produced no detector trigger because each poll cycle
// saw exactly 1 missed key (within the 40 ms cadence keystrokes don't
// overlap) and the original per-poll comparison absorbed every 1-drift
// into tolerance. With cumulative semantics, drift carries across polls
// until accum > tolerance and pendingVks_ holds the buffered misses.
//
// Verifies the realistic bypass timeline:
//   Poll N    : press 'A' (no hook fire). accum=1 → no trigger.
//   Poll N+1  : release 'A'. drift=0. accum stays 1.
//   Poll N+2  : press 'S' (no hook fire). accum=2 > 1 → TRIGGER.
//   Inject in observed order: 'a' then 's'. One reinstall.
TEST_F(HookHijackDetectorTest, SequentialSingleKeys_AccumulateAndTrigger) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();   // baseline (all keys up)

    // Poll N: press 'A' (Dorion eats — hook stays at 0).
    PressKey('A');
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 0)
        << "Single-key drift must accumulate, not trigger immediately.";
    EXPECT_TRUE(injectedGhosts_.empty());

    // Poll N+1: 'A' released. No new presses; no hook fires. accum unchanged.
    ReleaseKey('A');
    detector.Poll();
    EXPECT_EQ(reinstallCount_, 0);

    // Poll N+2: press 'S' (Dorion still eats). accum should become 2.
    PressKey('S');
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 1)
        << "Two sequential missed keys must trigger one cumulative reinstall.";
    ASSERT_EQ(injectedGhosts_.size(), 2u)
        << "Pending buffer must retain both 'A' and 'S' across polls.";
    EXPECT_EQ(injectedGhosts_[0], L'a');
    EXPECT_EQ(injectedGhosts_[1], L's');
}

// ── 12. After trigger, accum + buffer reset cleanly for the next session ─
// Verifies the post-trigger state: accum=0, pendingVks_ empty, ready to
// detect the next bypass episode without spurious carry-over from the
// previous one.
TEST_F(HookHijackDetectorTest, AfterTrigger_StateResetsCleanly) {
    NextKey::HookHijackDetector detector(MakeCallbacks());
    detector.SetChromiumClassActive(true);
    detector.Poll();   // baseline

    // First bypass episode — fire trigger via 2-key burst.
    PressKey('A');
    PressKey('S');
    detector.Poll();
    ASSERT_EQ(reinstallCount_, 1);
    ASSERT_EQ(injectedGhosts_.size(), 2u);

    // Simulate hook reinstall succeeding — release keys, hook starts firing.
    ReleaseKey('A');
    ReleaseKey('S');
    detector.Poll();   // post-trigger state should be clean now

    // Single new key WITH hook firing — no drift, no trigger.
    PressKey('D');
    mockHookFireCount_ += 1;
    detector.Poll();

    EXPECT_EQ(reinstallCount_, 1)
        << "Hook recovered; no second trigger should fire.";
    EXPECT_EQ(injectedGhosts_.size(), 2u)
        << "Buffer must be empty after the previous trigger flushed it.";
}

}  // namespace
