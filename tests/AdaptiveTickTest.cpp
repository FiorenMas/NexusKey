// Tests for ComputeTickInterval — active-until-deep-idle cadence + STOP.
// Linux-portable: no Win32, no threads. Pins the boundary mapping that drives
// MainThreadWorker cadence retune in HookEngine::RetuneCadenceIfNeeded.
//
// Plan reference: docs/plans/2026-05-30-idle-ram-investigation-summary.md
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "app/system/AdaptiveTick.h"

using NextKey::ComputeTickInterval;
using NextKey::kIdleStopThreshMs;
using NextKey::kTickActiveMs;

TEST(AdaptiveTickTest, ZeroIdle_ReturnsActive) {
    EXPECT_EQ(ComputeTickInterval(0), std::chrono::milliseconds(kTickActiveMs));
}

TEST(AdaptiveTickTest, ShortAndMediumPause_StayActive) {
    // A pause of seconds — or even up to a minute — must stay at the active
    // 200ms cadence so CJK (Win+Space) is still caught instantly. This is the
    // a8800b3f behavior the 2026-05-27 adaptive backoff regressed (it dropped
    // to 1s/5s here, making layout switches lag).
    EXPECT_EQ(ComputeTickInterval(5000),  std::chrono::milliseconds(kTickActiveMs));
    EXPECT_EQ(ComputeTickInterval(30000), std::chrono::milliseconds(kTickActiveMs));
    EXPECT_EQ(ComputeTickInterval(90000), std::chrono::milliseconds(kTickActiveMs));
}

TEST(AdaptiveTickTest, JustBelowStopThreshold_StaysActive) {
    EXPECT_EQ(ComputeTickInterval(kIdleStopThreshMs - 1),
              std::chrono::milliseconds(kTickActiveMs));
}

TEST(AdaptiveTickTest, AtStopThreshold_ReturnsStop) {
    // 0 ms is the STOP sentinel — owner maps it to SetTickInterval(0) so the
    // worker parks (cv_.wait ∞) and Windows trims the working set.
    EXPECT_EQ(ComputeTickInterval(kIdleStopThreshMs), std::chrono::milliseconds(0));
}

TEST(AdaptiveTickTest, FarPastStopThreshold_StaysStopped) {
    // 24 hours of idle — must stay STOPPED (0), not roll over (no wrap / off-by-one).
    EXPECT_EQ(ComputeTickInterval(24ULL * 60 * 60 * 1000), std::chrono::milliseconds(0));
}

TEST(AdaptiveTickTest, ConstantsMatchDocumentedValues) {
    // Pin the documented values so a stealth retune forces a deliberate test
    // (and doc) update.
    EXPECT_EQ(kTickActiveMs,     200u);
    EXPECT_EQ(kIdleStopThreshMs, 120000u);
}
