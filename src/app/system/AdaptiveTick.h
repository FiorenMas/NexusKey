// AdaptiveTick.h - idle cadence for MainThreadWorker.
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pure C++, Linux-portable. Maps "milliseconds since last user activity" to
// the MainThreadWorker tick interval. Used by HookEngine::OnTickPoll and
// HookEngine::RetuneCadenceIfNeeded.
//
// Two states only:
//   * Active / briefly paused (idle < kIdleStopThreshMs): tick at 200 ms. The
//     worker polls CJK layout (Win+Space), foreground PID, and the anti-Dorion
//     detector at this cadence — so a layout switch is caught within 200 ms
//     (the instant feel of the a8800b3f build).
//   * Deep idle (idle >= kIdleStopThreshMs): STOP (return 0). The owner maps 0
//     to SetTickInterval(0) → the worker blocks on cv_.wait (∞), touches no
//     pages, and Windows trims the working set (v2.1.24 idle parity). The next
//     keystroke resumes the cadence via HookEngine::MarkActivity → Signal.
//
// History: the 2026-05-27 "adaptive backoff" (200ms → 1s → 5s on idle) was
// REMOVED. It regressed CJK detection (Win+Space after a pause caught 1-5s
// late) for ZERO RAM benefit — touching pages every few seconds never lets the
// working set age out; only a full STOP does. See
// docs/plans/2026-05-30-idle-ram-investigation-summary.md.

#pragma once

#include <chrono>
#include <cstdint>

namespace NextKey {

// Active tick cadence (ms) — used while the user is interacting or only
// briefly paused. 200 ms makes CJK layout (Win+Space) detection feel instant.
inline constexpr std::uint32_t kTickActiveMs = 200;

// Deep-idle threshold (ms since last MarkActivity). Past this with no input,
// the worker STOPS (parks) so Windows can trim the working set. Tunable: lower
// (e.g. 60000) to trim idle machines sooner, at the cost of a slightly more
// frequent (harmless) resume refault.
inline constexpr std::uint64_t kIdleStopThreshMs = 120000;  // 2 min

/// Given milliseconds since last user activity, return the tick interval the
/// MainThreadWorker should use. Pure function; no globals, no Win32. Boundary
/// behavior pinned by tests/AdaptiveTickTest.cpp.
///
/// Returns kTickActiveMs while idle < kIdleStopThreshMs, else **0 ms = STOP**
/// (the owner parks the worker on cv_.wait ∞ → Windows trims the WS; a
/// keystroke resumes the cadence). 0 is the only state that actually trims;
/// gradual backoff was removed (it broke instant CJK for no RAM gain).
[[nodiscard]] constexpr std::chrono::milliseconds
ComputeTickInterval(std::uint64_t idleMs) noexcept {
    if (idleMs < kIdleStopThreshMs) {
        return std::chrono::milliseconds(kTickActiveMs);
    }
    return std::chrono::milliseconds(0);  // deep idle → STOP (park; WS trims)
}

}  // namespace NextKey
