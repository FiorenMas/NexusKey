// VKey - MainThreadWorker
// SPDX-License-Identifier: AGPL-3.0-only
//
// Sprint 1 Phase C — single home for non-hot-path work that previously
// executed on the hook thread (config reload, focus poll, heartbeat,
// CJK detection). Moves these off the LL hook callback chain so Rule #11
// is honoured at the call-graph level (D7 audit only sees direct entry
// bodies; ProcessKeyDown -> QuickSyncFromSharedState transitively
// acquires stateMutex_, which Phase C eliminates).
//
// Phase C lands in three pieces:
//   D8 — scaffolding (this file): empty thread that idles until Stop.
//   D9 — wire ConfigEvent so config reload happens here, not on hook.
//   D10 — wire heartbeat + CJK layout poll, retire the SetTimer(200 ms).

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace NextKey {

class MainThreadWorker {
public:
    /// Handler invoked on the worker thread when Signal() fires (or on
    /// the first wake after Start() if Signal was called pre-Start).
    /// Owner-provided; coalescing is allowed — if N rapid signals
    /// arrive while the handler is running, the worker will dispatch
    /// at most one additional invocation. Exceptions thrown from the
    /// handler are caught and swallowed; the worker continues running.
    using WorkHandler = std::function<void()>;

    MainThreadWorker() = default;
    ~MainThreadWorker();

    MainThreadWorker(const MainThreadWorker&) = delete;
    MainThreadWorker& operator=(const MainThreadWorker&) = delete;
    MainThreadWorker(MainThreadWorker&&) = delete;
    MainThreadWorker& operator=(MainThreadWorker&&) = delete;

    /// Launch the worker thread. Returns false if already running
    /// (idempotent — second call while running is a no-op). If Signal
    /// was called before Start, the latched signal is dispatched on
    /// first wake.
    bool Start();

    /// Signal shutdown and join the worker thread. Idempotent: safe
    /// before Start, after Stop, and from the destructor. Returns once
    /// the worker thread has finished (within the wakeup latency of the
    /// wait primitive — typically a few µs).
    void Stop() noexcept;

    /// True between Start() returning true and Stop() (or destruction)
    /// completing.
    [[nodiscard]] bool IsRunning() const noexcept;

    /// Register the work handler. Safe to call before Start, while
    /// running, or after Stop. Calling while running takes effect on
    /// the next Signal() (the in-flight invocation, if any, finishes
    /// with the previous handler).
    void SetWorkHandler(WorkHandler handler);

    /// Wake the worker. Coalescing: multiple rapid signals while the
    /// handler is running collapse to at most one extra dispatch. Safe
    /// to call from any thread, including before Start (latched) and
    /// after Stop (no-op).
    void Signal() noexcept;

    /// Register the periodic-tick handler. The tick handler runs on
    /// the worker thread every tick interval (see SetTickInterval) as
    /// long as Start has been called and Stop has not. Like the work
    /// handler, exceptions are caught and swallowed; setting a new
    /// handler while running takes effect on the next tick.
    void SetTickHandler(WorkHandler handler);

    /// Set the tick interval. A non-zero interval enables periodic
    /// ticks; an interval of zero (the default) disables them. Safe to
    /// call before Start, while running, or after Stop. Changes take
    /// effect on the next wait cycle (no shorter than the previously-
    /// configured interval if a wait is already in flight).
    void SetTickInterval(std::chrono::milliseconds interval) noexcept;

private:
    void Run() noexcept;

    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopRequested_ = false;
    bool workPending_ = false;
    WorkHandler workHandler_;
    WorkHandler tickHandler_;
    std::chrono::milliseconds tickInterval_{0};
    std::atomic<bool> running_{false};
};

}  // namespace NextKey
