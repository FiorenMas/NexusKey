// VKey - Reinstall Burst Scheduler Implementation (Anti-Dorion v2, 2026-05-28)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Linux-portable: no <Windows.h>. Production wires the schedule callback
// to Win32 CreateTimerQueueTimer; tests inject a deterministic mock.

#include "ReinstallBurstScheduler.h"

namespace NextKey {

ReinstallBurstScheduler::ReinstallBurstScheduler(Callbacks callbacks) noexcept
    : callbacks_(std::move(callbacks)) {}

void ReinstallBurstScheduler::Schedule(uint32_t reason,
                                        std::vector<uint32_t> delaysMs) noexcept {
    // Snapshot the current generation. Each scheduled callback captures
    // this snapshot and re-checks at fire time — Cancel() bumps the
    // generation, making the captured value stale → callback no-ops.
    const uint64_t scheduledGen = generation_.load(std::memory_order_acquire);

    for (uint32_t delay : delaysMs) {
        // Note: we capture `this` raw — the scheduler outlives the burst
        // (owner lifetime); Cancel() at owner destruction is the documented
        // teardown ordering. If the owner is destroyed before pending fires
        // land, that's a contract violation — not something Schedule itself
        // defends against (would require shared_ptr machinery for what is
        // a no-cycle ownership graph in production).
        callbacks_.schedule(delay, [this, scheduledGen, reason]() noexcept {
            if (generation_.load(std::memory_order_acquire) != scheduledGen) {
                return;  // burst was cancelled (focus left chromium app)
            }
            callbacks_.postReinstall(reason);
        });
    }
}

void ReinstallBurstScheduler::Cancel() noexcept {
    // Bump generation — every in-flight scheduled callback sees the
    // mismatch and skips. Release ordering so the bump publishes before
    // a subsequent Schedule snapshots a fresh generation.
    generation_.fetch_add(1, std::memory_order_acq_rel);
}

}  // namespace NextKey
