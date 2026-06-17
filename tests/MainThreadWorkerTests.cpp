// MainThreadWorkerTests.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Sprint 1 D8: scaffolding contract tests for MainThreadWorker.
//
// D8 is "start/stop only" — no work items yet. These tests lock in the
// lifecycle contract that D9 (config-event channel) and D10 (heartbeat +
// CJK poll) will extend without rewriting:
//   - Start launches a worker thread; IsRunning reports true.
//   - Stop signals shutdown and joins; IsRunning reports false.
//   - Stop completes within the < 100 ms DoD budget (plan §C D8).
//   - Repeated Start / Stop / destructor calls are idempotent and safe.
//   - Destructor implies Stop (RAII; no thread leak).
//
// MainThreadWorker is portable (std::thread + condition_variable) so this
// suite runs on the Linux test build. D9 will swap the internal wait
// primitive to Win32 WaitForMultipleObjects when ConfigEvent is wired in;
// the Start/Stop/IsRunning public contract this suite checks must still
// hold then.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "app/system/MainThreadWorker.h"

namespace NextKey {
namespace {

using namespace std::chrono_literals;

class MainThreadWorkerTest : public ::testing::Test {
protected:
    MainThreadWorker worker_;
};

TEST_F(MainThreadWorkerTest, StartLaunchesThread_StopJoinsCleanly) {
    EXPECT_FALSE(worker_.IsRunning());
    EXPECT_TRUE(worker_.Start());
    EXPECT_TRUE(worker_.IsRunning());

    const auto t0 = std::chrono::steady_clock::now();
    worker_.Stop();
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_FALSE(worker_.IsRunning());
    // DoD: start/stop completes in < 100 ms (plan §C D8).
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100);
}

TEST_F(MainThreadWorkerTest, SecondStartWhileRunningReturnsFalse) {
    ASSERT_TRUE(worker_.Start());
    EXPECT_FALSE(worker_.Start());  // already running
    EXPECT_TRUE(worker_.IsRunning());
    worker_.Stop();
}

TEST_F(MainThreadWorkerTest, StopWithoutStartIsNoOp) {
    EXPECT_FALSE(worker_.IsRunning());
    EXPECT_NO_THROW(worker_.Stop());
    EXPECT_FALSE(worker_.IsRunning());
}

TEST_F(MainThreadWorkerTest, StopAfterStopIsNoOp) {
    ASSERT_TRUE(worker_.Start());
    worker_.Stop();
    EXPECT_FALSE(worker_.IsRunning());
    EXPECT_NO_THROW(worker_.Stop());
    EXPECT_FALSE(worker_.IsRunning());
}

TEST_F(MainThreadWorkerTest, MultipleStartStopCyclesWork) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(worker_.Start()) << "cycle " << i;
        EXPECT_TRUE(worker_.IsRunning()) << "cycle " << i;
        worker_.Stop();
        EXPECT_FALSE(worker_.IsRunning()) << "cycle " << i;
    }
}

TEST(MainThreadWorkerLifetimeTest, DestructorImpliesStop_NoThreadLeak) {
    // Worker goes out of scope while running — destructor must Stop+join.
    // If RAII is broken, the thread leaks and ASan/TSan / process exit
    // detect a dangling thread; here we just check the destructor returns.
    {
        MainThreadWorker w;
        ASSERT_TRUE(w.Start());
        ASSERT_TRUE(w.IsRunning());
        // Let the worker reach its idle wait before destruction.
        std::this_thread::sleep_for(5ms);
    }
    // If we get here without the test runner hanging, RAII held.
    SUCCEED();
}

TEST(MainThreadWorkerLifetimeTest, RapidStartStop_NoDeadlockOrCrash) {
    // Stress the lifecycle to surface any race in the start/stop signal.
    for (int i = 0; i < 50; ++i) {
        MainThreadWorker w;
        ASSERT_TRUE(w.Start()) << "iteration " << i;
        w.Stop();
    }
}

// ─────────────────────────────────────────────────────────────────────
// D9 — Signal / WorkHandler dispatch
//
// Worker exposes Signal() and SetWorkHandler(callable). When Signal()
// fires, the worker invokes the registered handler on its own thread.
// This is the channel that main.cpp's hookReloadCallback uses to push
// config-change handling off the main thread (and out of the
// QuickSyncFromSharedState hook-thread race window).
// ─────────────────────────────────────────────────────────────────────

namespace {

// Helper: waits for the handler to fire or times out. Returns true if fired.
struct HandlerLatch {
    std::atomic<int> count{0};
    std::mutex mu;
    std::condition_variable cv;

    void Fire() {
        count.fetch_add(1, std::memory_order_release);
        std::lock_guard<std::mutex> lock(mu);
        cv.notify_all();
    }

    bool WaitFor(int target, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mu);
        return cv.wait_for(lock, timeout, [&] {
            return count.load(std::memory_order_acquire) >= target;
        });
    }
};

}  // namespace

TEST_F(MainThreadWorkerTest, Signal_HandlerInvokedWithin50ms) {
    HandlerLatch latch;
    worker_.SetWorkHandler([&latch] { latch.Fire(); });
    ASSERT_TRUE(worker_.Start());

    const auto t0 = std::chrono::steady_clock::now();
    worker_.Signal();
    EXPECT_TRUE(latch.WaitFor(1, 50ms));
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 50);
    EXPECT_EQ(latch.count.load(), 1);

    worker_.Stop();
}

TEST_F(MainThreadWorkerTest, MultipleSignalsCoalesce_HandlerRunsAtLeastOnce) {
    // Coalescing semantics: the handler reads SharedState which already
    // reflects the latest change, so N rapid Signal() calls are allowed
    // to collapse to fewer handler invocations. We only require ≥ 1.
    HandlerLatch latch;
    worker_.SetWorkHandler([&latch] { latch.Fire(); });
    ASSERT_TRUE(worker_.Start());

    for (int i = 0; i < 10; ++i) worker_.Signal();
    EXPECT_TRUE(latch.WaitFor(1, 50ms));
    // Don't pin the count — the worker may dispatch once or up to 10×.
    EXPECT_GE(latch.count.load(), 1);

    worker_.Stop();
}

TEST_F(MainThreadWorkerTest, SignalWithoutHandler_NoCrash) {
    ASSERT_TRUE(worker_.Start());
    worker_.Signal();
    // Give worker a tick to wake; with no handler, it just no-ops.
    std::this_thread::sleep_for(20ms);
    worker_.Stop();
    SUCCEED();
}

TEST_F(MainThreadWorkerTest, SignalBeforeStart_LatchedAndDispatchedOnStart) {
    HandlerLatch latch;
    worker_.SetWorkHandler([&latch] { latch.Fire(); });

    worker_.Signal();  // pre-start signal — should latch
    ASSERT_TRUE(worker_.Start());
    EXPECT_TRUE(latch.WaitFor(1, 50ms));

    worker_.Stop();
}

TEST_F(MainThreadWorkerTest, SignalAfterStop_NoOp) {
    HandlerLatch latch;
    worker_.SetWorkHandler([&latch] { latch.Fire(); });
    ASSERT_TRUE(worker_.Start());
    worker_.Stop();

    worker_.Signal();
    std::this_thread::sleep_for(20ms);
    EXPECT_EQ(latch.count.load(), 0);
}

TEST_F(MainThreadWorkerTest, SetHandlerWhileRunning_NewHandlerSeenOnNextSignal) {
    HandlerLatch latchA, latchB;
    worker_.SetWorkHandler([&latchA] { latchA.Fire(); });
    ASSERT_TRUE(worker_.Start());

    worker_.Signal();
    EXPECT_TRUE(latchA.WaitFor(1, 50ms));

    worker_.SetWorkHandler([&latchB] { latchB.Fire(); });
    worker_.Signal();
    EXPECT_TRUE(latchB.WaitFor(1, 50ms));

    worker_.Stop();
}

// ─────────────────────────────────────────────────────────────────────
// D10 — periodic tick (CJK poll + heartbeat replacement)
//
// Worker grows SetTickHandler + SetTickInterval. The wait primitive
// becomes wait_for(interval): the timeout branch dispatches the tick
// handler, the predicate branch dispatches the work handler (D9) or
// exits on stop. Tick is independent from Signal: a Signal fired
// during a tick interval still wakes the work handler, and a tick
// fired while a Signal is pending still runs the work handler at
// least once on the same wake.
// ─────────────────────────────────────────────────────────────────────

TEST_F(MainThreadWorkerTest, TickHandler_FiresAtConfiguredInterval) {
    HandlerLatch latch;
    worker_.SetTickHandler([&latch] { latch.Fire(); });
    worker_.SetTickInterval(std::chrono::milliseconds(50));
    ASSERT_TRUE(worker_.Start());

    // Expect ≥ 3 ticks within 250 ms (3 × 50 ms = 150 ms; 100 ms slack
    // for scheduler jitter).
    EXPECT_TRUE(latch.WaitFor(3, 250ms));
    worker_.Stop();
    EXPECT_GE(latch.count.load(), 3);
}

TEST_F(MainThreadWorkerTest, NoTickWhenIntervalUnset) {
    HandlerLatch latch;
    worker_.SetTickHandler([&latch] { latch.Fire(); });
    // Interval not set — defaults to disabled.
    ASSERT_TRUE(worker_.Start());
    std::this_thread::sleep_for(100ms);
    worker_.Stop();
    EXPECT_EQ(latch.count.load(), 0);
}

TEST_F(MainThreadWorkerTest, NoTickWhenHandlerUnset) {
    // Setting an interval without a handler must not crash on tick.
    worker_.SetTickInterval(std::chrono::milliseconds(20));
    ASSERT_TRUE(worker_.Start());
    std::this_thread::sleep_for(80ms);
    worker_.Stop();
    SUCCEED();
}

TEST_F(MainThreadWorkerTest, TickAndSignalCoexist_BothInvoked) {
    HandlerLatch tickLatch;
    HandlerLatch workLatch;
    worker_.SetTickHandler([&tickLatch] { tickLatch.Fire(); });
    worker_.SetWorkHandler([&workLatch] { workLatch.Fire(); });
    worker_.SetTickInterval(std::chrono::milliseconds(40));
    ASSERT_TRUE(worker_.Start());

    worker_.Signal();
    EXPECT_TRUE(workLatch.WaitFor(1, 100ms));
    EXPECT_TRUE(tickLatch.WaitFor(1, 200ms));
    worker_.Stop();
}

TEST_F(MainThreadWorkerTest, ChangeTickIntervalWhileRunning_TakesEffectNextWait) {
    HandlerLatch latch;
    worker_.SetTickHandler([&latch] { latch.Fire(); });
    worker_.SetTickInterval(std::chrono::milliseconds(200));  // slow
    ASSERT_TRUE(worker_.Start());

    // Speed up — the next wait cycle should pick up the new interval.
    worker_.SetTickInterval(std::chrono::milliseconds(20));
    EXPECT_TRUE(latch.WaitFor(3, 250ms));
    worker_.Stop();
}

TEST_F(MainThreadWorkerTest, TickHandlerExceptionDoesNotKillWorker) {
    std::atomic<int> throwCount{0};
    HandlerLatch healthyLatch;

    worker_.SetTickHandler([&] {
        throwCount.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("tick failure");
    });
    worker_.SetTickInterval(std::chrono::milliseconds(20));
    ASSERT_TRUE(worker_.Start());

    std::this_thread::sleep_for(80ms);
    EXPECT_GE(throwCount.load(), 2);
    EXPECT_TRUE(worker_.IsRunning());

    worker_.SetTickHandler([&healthyLatch] { healthyLatch.Fire(); });
    EXPECT_TRUE(healthyLatch.WaitFor(1, 100ms));
    worker_.Stop();
}

TEST_F(MainThreadWorkerTest, HandlerExceptionDoesNotKillWorker) {
    // If a handler throws, the worker must keep running and continue
    // dispatching subsequent signals. (Owner code is std::function — a
    // throw must not propagate out of the worker thread and abort.)
    std::atomic<int> normalCalls{0};
    std::atomic<int> throwCalls{0};
    HandlerLatch normalLatch;

    worker_.SetWorkHandler([&] {
        throwCalls.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("test");
    });
    ASSERT_TRUE(worker_.Start());

    worker_.Signal();
    // Give the worker time to wake, run the throwing handler, and recover.
    std::this_thread::sleep_for(20ms);
    EXPECT_GE(throwCalls.load(), 1);
    EXPECT_TRUE(worker_.IsRunning());

    // Replace the handler and signal again — must still be invoked.
    worker_.SetWorkHandler([&] {
        normalCalls.fetch_add(1, std::memory_order_relaxed);
        normalLatch.Fire();
    });
    worker_.Signal();
    EXPECT_TRUE(normalLatch.WaitFor(1, 50ms));
    EXPECT_GE(normalCalls.load(), 1);

    worker_.Stop();
}

// Adaptive-tick assumption check (2026-05-27, plan §3.3): the path the
// adaptive-tick plan depends on is:
//   1. Hook thread (after idle) calls MarkActivity -> workerSignalFn_() ->
//      MainThreadWorker::Signal which sets workPending_=true + notify_all.
//   2. Worker wakes (predicate true), runs workHandler.
//   3. workHandler (in the plan's wiring) calls RetuneCadenceIfNeeded which
//      calls SetTickInterval(200ms) - this happens WHILE worker is outside
//      wait_for, so it only updates tickInterval_; no wake is needed.
//   4. workHandler returns; the worker loop iterates and the next wait_for
//      reads the NEW tickInterval_ (200 ms), not the old one (5 s).
//
// This test pins step 4: after a workHandler runs that mutates tickInterval_,
// the immediately following wait must use the new value. If it instead
// reused the old value, the post-idle CJK detection would lag by up to 5 s.
//
// Note: an earlier draft of this test checked whether SetTickInterval called
// from a *different* thread woke an in-flight wait_for. It does not (predicate
// is still false after notify, so wait_for re-blocks until its original
// deadline). The misleading comment at MainThreadWorker.cpp:82-86 about
// notify_all "picking up the new interval" is technically wrong — but the
// adaptive-tick plan doesn't rely on that path; it goes through Signal.
TEST_F(MainThreadWorkerTest, WorkHandlerChangingTickInterval_NextWaitUsesNewValue) {
    std::atomic<int> tickCount{0};
    worker_.SetTickHandler([&] { tickCount.fetch_add(1, std::memory_order_relaxed); });

    // workHandler mutates tickInterval_ on its first run (simulates
    // RetuneCadenceIfNeeded shrinking back to active cadence after Signal).
    std::atomic<int> workCount{0};
    worker_.SetWorkHandler([this, &workCount] {
        if (workCount.fetch_add(1, std::memory_order_relaxed) == 0) {
            worker_.SetTickInterval(50ms);  // shrink from 5 s -> 50 ms
        }
    });

    // Long initial interval — without the Signal path, no tick fires.
    worker_.SetTickInterval(5'000ms);
    worker_.Start();

    // Confirm we're idle in wait_for(5s) — no tick yet.
    std::this_thread::sleep_for(80ms);
    EXPECT_EQ(tickCount.load(), 0);

    // Signal wakes the worker (workPending_=true -> predicate true).
    // workHandler runs and shortens tickInterval_ to 50 ms.
    worker_.Signal();

    // Within 400 ms the worker should have run workHandler (~immediate) and
    // then completed at least one tick at the new 50 ms cadence.
    std::this_thread::sleep_for(400ms);
    EXPECT_EQ(workCount.load(), 1) << "workHandler did not run after Signal.";
    EXPECT_GE(tickCount.load(), 1)
        << "tickInterval_ mutation inside workHandler did not take effect on "
        << "the next wait_for - adaptive plan's post-idle resume path breaks.";

    worker_.Stop();
}

}  // namespace
}  // namespace NextKey
