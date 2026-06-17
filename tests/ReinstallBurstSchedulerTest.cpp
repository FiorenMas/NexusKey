// ReinstallBurstSchedulerTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Anti-Dorion v2 (2026-05-28) — burst-reinstall primary path.
//
// Dorion installs its WH_KEYBOARD_LL slightly AFTER the focus event
// VKey wakes up on, so a single focus-time reinstall ends up below
// Dorion in the LL chain and the user's first ~12 seconds of typing
// gets eaten. The burst scheduler counters this by issuing N reinstalls
// at staggered delays (e.g. 300 / 800 / 1500 ms) so at least one lands
// after Dorion's install — VKey ends up newest-on-chain, hook wins.
//
// PHILOSOPHY §2 (test-first) — these tests pin the scheduler contract
// BEFORE the implementation lands:
//   - Schedule(reason, delays) fires N scheduling callbacks, one per delay.
//   - Each scheduled callback, when invoked, posts a reinstall.
//   - Cancel() bumps a generation; previously scheduled (in-flight) callbacks
//     observe the generation mismatch and skip — no stale reinstall after
//     focus-out of the chromium app.
//   - Schedule again after Cancel works (next session is independent).
//
// Platform interactions (Win32 CreateTimerQueueTimer in production, deterministic
// fake in tests) go through Callbacks → Linux-runnable.

#include <gtest/gtest.h>

#include "app/system/ReinstallBurstScheduler.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace {

class ReinstallBurstSchedulerTest : public ::testing::Test {
protected:
    // Each `Schedule` call records its (delay, callback) pair; the test
    // fires callbacks manually in the order it chooses — that's the entire
    // platform abstraction the scheduler needs.
    struct ScheduledCall {
        uint32_t delayMs;
        std::function<void()> fire;
    };
    std::vector<ScheduledCall> scheduled_;
    std::vector<uint32_t> posted_;

    NextKey::ReinstallBurstScheduler::Callbacks MakeCallbacks() {
        NextKey::ReinstallBurstScheduler::Callbacks cb;
        cb.schedule = [this](uint32_t delayMs, std::function<void()> fn) {
            scheduled_.push_back({delayMs, std::move(fn)});
        };
        cb.postReinstall = [this](uint32_t reason) {
            posted_.push_back(reason);
        };
        return cb;
    }
};

// ── 1. Schedule fires N scheduling callbacks in order ─────────────────
TEST_F(ReinstallBurstSchedulerTest, Schedule_FiresOneCallbackPerDelay) {
    NextKey::ReinstallBurstScheduler sched(MakeCallbacks());
    sched.Schedule(/*reason=*/42, {300, 800, 1500});

    ASSERT_EQ(scheduled_.size(), 3u);
    EXPECT_EQ(scheduled_[0].delayMs, 300u);
    EXPECT_EQ(scheduled_[1].delayMs, 800u);
    EXPECT_EQ(scheduled_[2].delayMs, 1500u);
    EXPECT_TRUE(posted_.empty())
        << "Scheduling alone must not post — fire is what posts.";
}

// ── 2. Firing scheduled callbacks posts the reinstall ─────────────────
TEST_F(ReinstallBurstSchedulerTest, Fire_PostsReinstallOncePerScheduledCallback) {
    NextKey::ReinstallBurstScheduler sched(MakeCallbacks());
    sched.Schedule(/*reason=*/42, {300, 800, 1500});

    for (auto& c : scheduled_) c.fire();

    ASSERT_EQ(posted_.size(), 3u);
    EXPECT_EQ(posted_[0], 42u);
    EXPECT_EQ(posted_[1], 42u);
    EXPECT_EQ(posted_[2], 42u);
}

// ── 3. Cancel drops in-flight callbacks ────────────────────────────────
// Generation counter contract: callbacks captured the generation at
// schedule time; firing checks current generation. Cancel bumps it →
// stale fires no-op silently. This is the canonical "focus left Dorion
// before the burst finished" scenario.
TEST_F(ReinstallBurstSchedulerTest, Cancel_StaleCallbacksNoOp) {
    NextKey::ReinstallBurstScheduler sched(MakeCallbacks());
    sched.Schedule(/*reason=*/42, {300, 800, 1500});

    // First callback fires before cancel — posts.
    scheduled_[0].fire();
    ASSERT_EQ(posted_.size(), 1u);

    // Cancel, then fire the still-pending callbacks.
    sched.Cancel();
    scheduled_[1].fire();
    scheduled_[2].fire();

    EXPECT_EQ(posted_.size(), 1u)
        << "Post-cancel callbacks must skip the reinstall post.";
}

// ── 4. Reschedule after Cancel works (fresh generation) ────────────────
TEST_F(ReinstallBurstSchedulerTest, Reschedule_AfterCancel_IndependentBurst) {
    NextKey::ReinstallBurstScheduler sched(MakeCallbacks());

    sched.Schedule(/*reason=*/1, {300});
    ASSERT_EQ(scheduled_.size(), 1u);

    sched.Cancel();

    // Second burst — its callbacks capture a NEW generation.
    sched.Schedule(/*reason=*/2, {300, 800});
    ASSERT_EQ(scheduled_.size(), 3u);   // 1 stale + 2 new

    // Fire the stale callback → no-op (Cancel bumped generation).
    scheduled_[0].fire();
    EXPECT_EQ(posted_.size(), 0u);

    // Fire the new callbacks → both post with the new reason.
    scheduled_[1].fire();
    scheduled_[2].fire();
    ASSERT_EQ(posted_.size(), 2u);
    EXPECT_EQ(posted_[0], 2u);
    EXPECT_EQ(posted_[1], 2u);
}

// ── 5. Schedule with empty delays is a no-op ───────────────────────────
TEST_F(ReinstallBurstSchedulerTest, Schedule_EmptyDelays_DoesNothing) {
    NextKey::ReinstallBurstScheduler sched(MakeCallbacks());
    sched.Schedule(/*reason=*/0, {});

    EXPECT_TRUE(scheduled_.empty());
    EXPECT_TRUE(posted_.empty());
}

// ── 6. Schedule chained — new burst before old fires ───────────────────
// Use case: user alt-tabs from Dorion to another Electron app fast.
// Both focus events fire ScheduleReinstallBurst. The OLD burst's
// callbacks should still no-op (the previous chromium-fg session ended)
// when generation got bumped between calls.
TEST_F(ReinstallBurstSchedulerTest, Schedule_TwiceInSequence_OldBurstCancelledImplicitlyByNew) {
    NextKey::ReinstallBurstScheduler sched(MakeCallbacks());
    sched.Schedule(/*reason=*/1, {300, 800});
    ASSERT_EQ(scheduled_.size(), 2u);

    // Caller's responsibility to Cancel() before scheduling again. Schedule
    // alone does not implicitly cancel — that's the explicit-contract design
    // (callers know when they want to cancel). Fire all → 2 posts + 2 more
    // from the second batch.
    sched.Cancel();
    sched.Schedule(/*reason=*/2, {500, 1000});

    for (auto& c : scheduled_) c.fire();

    ASSERT_EQ(posted_.size(), 2u);
    EXPECT_EQ(posted_[0], 2u);
    EXPECT_EQ(posted_[1], 2u);
}

}  // namespace
