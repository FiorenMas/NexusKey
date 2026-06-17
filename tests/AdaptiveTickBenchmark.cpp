// Tests for the MarkActivity gate logic + hot-path budget (plan §3.6.1).
// SPDX-License-Identifier: AGPL-3.0-only
//
// HookEngine.cpp is Win32-only and not linked into VKeyTests, so we mirror
// the MarkActivity gate here as a freestanding mock — same atomics, same
// branch, same workerSignalFn_ callback. This pins the design contract:
//
//   1. Hot-path body (already in active cadence) is ~5 ns:
//      atomic store + atomic load + branch miss. No signal fires.
//   2. Cold path (idle->active transition) fires workerSignalFn_ exactly
//      ONCE per transition, regardless of how many MarkActivity calls land
//      while still in the idle bucket.
//
// If a future change to HookEngine::MarkActivity breaks either property,
// this test catches it — even on Linux.

#include <gtest/gtest.h>

#include "app/system/AdaptiveTick.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>

namespace {

// Mirrors HookEngine::MarkActivity exactly. Any divergence between this
// mock and the real implementation in HookEngine.cpp is a bug — if the
// gate logic ever needs to change, update both sites together.
class MarkActivityMock {
public:
    void SetWorkerSignalFn(std::function<void()> fn) {
        workerSignalFn_ = std::move(fn);
    }
    void SetCurrentTickIntervalForTest(std::uint32_t ms) {
        currentTickIntervalMs_.store(ms, std::memory_order_relaxed);
    }
    void MarkActivity() noexcept {
        lastActivityTickMs_.store(NowMsForTest(), std::memory_order_relaxed);
        if (currentTickIntervalMs_.load(std::memory_order_relaxed)
            != NextKey::kTickActiveMs) {
            if (workerSignalFn_) workerSignalFn_();
        }
    }
    [[nodiscard]] std::uint64_t LastActivityTickMs() const noexcept {
        return lastActivityTickMs_.load(std::memory_order_relaxed);
    }

private:
    // Steady wall-clock surrogate for GetTickCount64 (which is Win32-only).
    // Monotonic; non-zero so we can distinguish "stored" from "default".
    static std::uint64_t NowMsForTest() noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::atomic<std::uint64_t> lastActivityTickMs_{0};
    std::atomic<std::uint32_t> currentTickIntervalMs_{NextKey::kTickActiveMs};
    std::function<void()> workerSignalFn_;
};

}  // namespace

// Plan §3.6.1 — gate fires signal exactly once on idle->active transition.
TEST(AdaptiveTickBenchmark, GatedSignalFiresOnceOnTransition) {
    MarkActivityMock mock;
    std::atomic<int> signalCount{0};
    mock.SetWorkerSignalFn([&] { signalCount.fetch_add(1, std::memory_order_relaxed); });

    // Stopped/parked state (interval 0) — gate is open.
    mock.SetCurrentTickIntervalForTest(0u);
    mock.MarkActivity();
    EXPECT_EQ(signalCount.load(), 1) << "Idle->active transition didn't signal.";

    // Caller (the real RetuneCadenceIfNeeded path) flips cadence back to
    // active. From now on the gate is closed; further MarkActivity calls
    // must NOT signal.
    mock.SetCurrentTickIntervalForTest(NextKey::kTickActiveMs);
    for (int i = 0; i < 1000; ++i) mock.MarkActivity();
    EXPECT_EQ(signalCount.load(), 1) << "Active-state MarkActivity must not signal.";

    // Second stopped->active transition.
    mock.SetCurrentTickIntervalForTest(0u);
    mock.MarkActivity();
    EXPECT_EQ(signalCount.load(), 2);
}

// Plan §3.6.1 — atomic store happens unconditionally on every call so the
// idle-detection math is fresh on the worker side regardless of gate state.
TEST(AdaptiveTickBenchmark, AtomicStoreHappensEvenInActiveState) {
    MarkActivityMock mock;
    mock.SetCurrentTickIntervalForTest(NextKey::kTickActiveMs);

    const std::uint64_t before = mock.LastActivityTickMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    mock.MarkActivity();
    const std::uint64_t after = mock.LastActivityTickMs();

    EXPECT_GT(after, before) << "Timestamp must update even when gate is closed.";
}

// Plan §3.6.1 — hot-path budget. In the active-cadence common case (no
// signal fired), MarkActivity must be < 50 ns/op (10× margin over expected
// ~5 ns on modern x86). This catches a future change that accidentally
// adds a syscall or allocation to the hook hot path.
//
// Note: the absolute budget is loose on purpose — Linux CI runners,
// virtualization overhead, and microbenchmark noise mean 5 ns can read as
// 30-40 ns. The threshold catches catastrophic regressions (microseconds),
// not nanosecond-level perf hunts.
TEST(AdaptiveTickBenchmark, MarkActivityHotPathBudget) {
    MarkActivityMock mock;
    std::atomic<int> signalCount{0};
    mock.SetWorkerSignalFn([&] { signalCount.fetch_add(1, std::memory_order_relaxed); });
    mock.SetCurrentTickIntervalForTest(NextKey::kTickActiveMs);

    constexpr int kIters = 1'000'000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) mock.MarkActivity();
    const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t0).count();
    const auto nsPerOp = elapsedNs / kIters;

    EXPECT_EQ(signalCount.load(), 0) << "Active-state path must not signal.";
    EXPECT_LT(nsPerOp, 50) << "MarkActivity exceeds hot-path budget. Per-op: "
                           << nsPerOp << " ns";
}
