// VKey - Reinstall Burst Scheduler (Anti-Dorion v2, 2026-05-28)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Primary path for the Anti-Dorion problem (see also HookHijackDetector
// which is now the safety net). Dorion installs its WH_KEYBOARD_LL
// sometime AFTER the focus event VKey wakes up on — empirically 100-
// 500 ms later, as the renderer thread spins up. A single focus-time
// reinstall on VKey's side lands UNDER Dorion in the chain → the user's
// keystrokes get eaten by Dorion's hook before VKey sees them.
//
// The fix: issue several reinstalls at staggered delays (300 / 800 /
// 1500 ms) so at least one runs AFTER Dorion's install. That reinstall
// puts VKey at the head of the chain — Dorion's hook is now below us,
// VKey's hook sees keys first, normal Telex transformation resumes.
//
// PHILOSOPHY compliance:
//   §2 (test-first) — all scheduling logic behind injectable callbacks
//      so the contract is Linux-testable without Win32 timers.
//   §3 (no dedicated thread) — production wires the schedule callback
//      to CreateTimerQueueTimer, which runs callbacks on the system
//      thread pool — zero VKey-owned threads added.
//   §12 (single-writer) — the scheduled callback calls `postReinstall`
//      which posts WM_APP_REINSTALL_HOOKS to the hook thread; no engine
//      state mutation happens from the timer thread.
//
// Generation-counter cancellation: Cancel() bumps a generation; each
// scheduled callback captures the generation it was scheduled under and
// no-ops if it observes a mismatch when firing. This way, a focus-out
// of Dorion before the burst finishes silently drops the pending
// reinstalls — no need to track timer handles for explicit cancel.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace NextKey {

class ReinstallBurstScheduler {
public:
    /// Callbacks the owner provides for platform plumbing. Production wires
    /// `schedule` to a Win32 thread-pool timer (CreateTimerQueueTimer) and
    /// `postReinstall` to `HookLifecycle::PostReinstallHooks`. Tests provide
    /// deterministic mocks that record scheduling without firing real
    /// timers, then invoke callbacks on demand.
    struct Callbacks {
        /// Schedule a one-shot callback to fire after `delayMs`. Production
        /// MAY fire on any thread; the callback's body (this scheduler's
        /// generation check + postReinstall) is thread-safe. Tests typically
        /// record (delay, callback) pairs and fire them on demand.
        std::function<void(uint32_t delayMs, std::function<void()> fire)> schedule;

        /// Post a reinstall. Production wires this to the hook lifecycle's
        /// PostReinstallHooks (which goes via PostThreadMessageW to the
        /// hook thread — the actual hook unhook+rehook happens there, not
        /// on the timer thread).
        std::function<void(uint32_t reason)> postReinstall;
    };

    explicit ReinstallBurstScheduler(Callbacks callbacks) noexcept;
    ~ReinstallBurstScheduler() noexcept = default;

    ReinstallBurstScheduler(const ReinstallBurstScheduler&)            = delete;
    ReinstallBurstScheduler& operator=(const ReinstallBurstScheduler&) = delete;
    ReinstallBurstScheduler(ReinstallBurstScheduler&&)                 = delete;
    ReinstallBurstScheduler& operator=(ReinstallBurstScheduler&&)      = delete;

    /// Schedule a burst of N reinstalls. Each delay in `delaysMs` produces
    /// one scheduled callback that, when fired, calls `postReinstall(reason)`
    /// — UNLESS Cancel() has been called in between, in which case the
    /// generation-mismatch check makes the callback no-op silently.
    void Schedule(uint32_t reason, std::vector<uint32_t> delaysMs) noexcept;

    /// Cancel pending callbacks. Bumps the generation counter; any callback
    /// scheduled before this call observes the mismatch and skips post.
    /// Idempotent.
    void Cancel() noexcept;

private:
    Callbacks callbacks_;
    // Monotonic counter. Each Schedule() reads it to capture a snapshot;
    // each scheduled callback's body re-reads it at fire time and skips
    // posting if they differ. Cancel() bumps it. Atomic because schedule
    // callbacks run on a thread pool thread (production) — read on fire,
    // write on Cancel.
    std::atomic<uint64_t> generation_{0};
};

}  // namespace NextKey
