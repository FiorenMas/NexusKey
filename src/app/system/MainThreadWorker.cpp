// VKey - MainThreadWorker
// SPDX-License-Identifier: AGPL-3.0-only
//
// See MainThreadWorker.h for the Phase C plan.

#include "app/system/MainThreadWorker.h"

#include <utility>

namespace NextKey {

MainThreadWorker::~MainThreadWorker() {
    Stop();
}

bool MainThreadWorker::Start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_.load(std::memory_order_acquire)) {
            return false;
        }
        stopRequested_ = false;
        // Note: workPending_ is intentionally NOT cleared here — a Signal
        // received before Start latches and dispatches on first wake.
        running_.store(true, std::memory_order_release);
    }

    thread_ = std::thread([this] { Run(); });
    return true;
}

void MainThreadWorker::Stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_acquire) && !thread_.joinable()) {
            // Stop without ever Start — drop any latched pre-Start signal.
            workPending_ = false;
            return;
        }
        stopRequested_ = true;
    }
    cv_.notify_all();

    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);

    // Clear any signal that arrived after the worker drained its last
    // dispatch — Signal()-after-Stop must not run the handler later.
    std::lock_guard<std::mutex> lock(mutex_);
    workPending_ = false;
}

bool MainThreadWorker::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void MainThreadWorker::SetWorkHandler(WorkHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    workHandler_ = std::move(handler);
}

void MainThreadWorker::Signal() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        workPending_ = true;
    }
    cv_.notify_all();
}

void MainThreadWorker::SetTickHandler(WorkHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    tickHandler_ = std::move(handler);
}

void MainThreadWorker::SetTickInterval(std::chrono::milliseconds interval) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tickInterval_ = interval;
    }
    // Wake the worker. NOTE: Run()'s wait predicate is (stopRequested_ ||
    // workPending_); a bare notify_all with neither flag set re-evaluates the
    // predicate, finds it false, and RE-BLOCKS until the ORIGINAL wait_for
    // deadline — so a mid-wait interval change is NOT picked up promptly here.
    // That is acceptable for the only production caller: RetuneCadenceIfNeeded
    // runs SetTickInterval on the worker thread itself (never mid-wait), and the
    // idle→active resume is driven separately via Signal() (which sets
    // workPending_, so it DOES break the wait). If a future caller needs a
    // prompt cross-thread shorten, route it through Signal() — do not rely on
    // this notify_all alone.
    cv_.notify_all();
}

void MainThreadWorker::Run() noexcept {
    auto invoke = [](const WorkHandler& h) noexcept {
        if (!h) return;
        try {
            h();
        } catch (...) {
            // Owner-provided handlers must not crash the worker. We
            // intentionally swallow — the alternative is std::terminate
            // through a noexcept boundary.
        }
    };

    for (;;) {
        WorkHandler workCopy;
        WorkHandler tickCopy;
        bool runWork = false;
        bool runTick = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto interval = tickInterval_;
            const auto predicate = [this] { return stopRequested_ || workPending_; };

            if (interval.count() > 0) {
                // Timeout-bounded wait: returns false on timeout, true if
                // predicate became true before the timeout. Distinguishes
                // tick (timeout) from work-signal / stop (predicate).
                const bool predicateMet = cv_.wait_for(lock, interval, predicate);
                if (stopRequested_) return;
                if (predicateMet) {
                    workPending_ = false;
                    runWork = true;
                    workCopy = workHandler_;
                } else {
                    runTick = true;
                    tickCopy = tickHandler_;
                }
            } else {
                cv_.wait(lock, predicate);
                if (stopRequested_) return;
                workPending_ = false;
                runWork = true;
                workCopy = workHandler_;
            }
        }

        if (runWork) invoke(workCopy);
        if (runTick) invoke(tickCopy);
    }
}

}  // namespace NextKey
