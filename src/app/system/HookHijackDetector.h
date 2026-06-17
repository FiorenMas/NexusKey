// VKey - Hook Hijack Detector (Anti-Dorion v2, 2026-05-28)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Stateless processor (no thread of its own — Pillar 2 "Nhẹ": one worker
// thread covers many responsibilities, one thread per responsibility is
// rejected). The owner (HookEngine) wires Poll() into MainThreadWorker's
// tick handler and retunes cadence to ~40ms when a Chromium-class app is
// foreground.
//
// Goal: detect when our WH_KEYBOARD_LL has been bypassed by a higher hook
// in the chain (Dorion's hijack). On detection, request a reinstall via
// the lifecycle and inject the missed keys retroactively into the engine
// so Vietnamese transformation stays correct across the bypass. The
// user's key #1 reaches Dorion as raw input (expendable since TV requires
// ≥2 keystrokes per transformed char); key #2 onwards hits the reinstated
// hook and the engine emits BS + transformed text that cleanly overwrites.
//
// Safety vs the reverted HookSelfHealer (742e16e, 2026-05-17): uses ONLY
// keyboard-state reads — no device registration, no RIDEV_INPUTSINK → no
// poison of own-process LL hook → VKey's own dialog typing keeps working.
// All platform interactions (GetKeyboardState, ToUnicodeEx) come in via
// injectable callbacks → Linux-testable.
//
// Threading contract: Poll() and SetChromiumClassActive() are called from
// the owner's tick / focus paths (typically MainThreadWorker thread for
// Poll; hook thread for SetChromiumClassActive). The detector itself owns
// no threads. Cross-thread state in members uses atomics; non-atomic
// members are only touched from Poll() (single-caller per owner contract).
//
// See docs/plans/2026-05-28-anti-dorion-detector-inject-design.md.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace NextKey {

class HookHijackDetector {
public:
    /// Callbacks the owner (HookEngine) provides for detector → engine
    /// plumbing and platform interactions. All six fields are required;
    /// the detector does not null-check — mis-construction is a programmer
    /// error, not a runtime case. Platform-touching callbacks (read keyboard
    /// state, translate VK→char) live here so Linux tests can mock them.
    struct Callbacks {
        /// Read the running keyboard-hook-fire counter (release-stored by
        /// LowLevelKeyboardProc on every keydown). The detector compares
        /// the delta against its polled up→down transition count; drift
        /// over the tolerance indicates our hook is bypassed.
        std::function<uint64_t()> readHookFireCount;

        /// Request an asynchronous keyboard-hook reinstall — production
        /// wires to `HookLifecycle::PostReinstallHooks(REINSTALL_REASON_HIJACK)`.
        /// Throttle is the lifecycle's responsibility, not the detector's.
        std::function<void()> requestReinstall;

        /// Inject one ghost character into the engine. Production routes
        /// via `HookLifecycle::PostGhostKey` → WM_APP_GHOSTKEY → hook
        /// thread → `HookEngine::HandleGhostChar` (single-writer §12).
        /// Called once per missed key, in observed order. The receiver
        /// only advances engine state (no SendInput / BS+replace) —
        /// detector is now a safety net behind burst reinstall, so the
        /// app's raw buffer ("as") is accepted as-is and the engine
        /// catches up so the NEXT real keystroke's ReplaceComposition
        /// has a sane baseline. Cost on detector trigger: 1-2 raw keys
        /// visible to the user before transformation resumes — matches
        /// EVKey behaviour, within the philosophy's "good enough"
        /// boundary.
        std::function<void(wchar_t)> injectGhostChar;

        /// Snapshot the system keyboard state into the 256-byte buffer.
        /// Production wires to `GetKeyboardState`; tests provide a mock
        /// buffer. Returns true on success, false on transient failure
        /// (detector skips the poll cycle).
        std::function<bool(uint8_t (&state)[256])> readKeyboardState;

        /// Translate a virtual-key code to wchar_t using the current
        /// keyboard layout + modifier state from `stateNow`. Production
        /// wires to `ToUnicodeEx` with the foreground thread's HKL;
        /// tests provide a deterministic mapping. Returns 0 for non-
        /// character keys or translation failure — caller skips inject.
        std::function<wchar_t(uint8_t vk, const uint8_t (&stateNow)[256])>
            translateVkToChar;
    };

    explicit HookHijackDetector(Callbacks callbacks) noexcept;
    ~HookHijackDetector() noexcept = default;

    HookHijackDetector(const HookHijackDetector&)            = delete;
    HookHijackDetector& operator=(const HookHijackDetector&) = delete;
    HookHijackDetector(HookHijackDetector&&)                 = delete;
    HookHijackDetector& operator=(HookHijackDetector&&)      = delete;

    /// Run one poll cycle. Owner calls this from its tick handler — the
    /// detector does not own a thread (Pillar 2). Re-checks the gate at
    /// entry; returns immediately if no Chromium-class app is foreground
    /// or if the post-reinstall cooldown is active. Idempotent against
    /// over-calling (no negative effect from a too-fast caller — cooldown
    /// + baseline math just no-ops the extra invocations).
    void Poll() noexcept;

    /// Re-establish polling baselines from the current keyboard state +
    /// hook-fire counter. Called on chromium-fg true→false→true transitions
    /// (Invariant 4: no ghost-key leak across sessions) and after focus
    /// changes that might leave stale state. Idempotent.
    void Reset() noexcept;

    /// Set whether a Chromium-class app is currently foreground. Called
    /// from the hook thread on every focus change. When FALSE, Poll()
    /// returns immediately (per-poll gate). A true → false transition
    /// also invalidates the next-poll baselines so the next chromium
    /// session starts fresh (Invariant 4).
    void SetChromiumClassActive(bool active) noexcept;

    /// True iff the per-poll gate is open. Diagnostic — owner uses this
    /// to decide whether to schedule Poll() at all (skipping reduces
    /// wakeups; the gate check inside Poll is the correctness guard).
    [[nodiscard]] bool IsActive() const noexcept {
        return chromiumClassActive_.load(std::memory_order_acquire);
    }

private:
    /// Snapshot current state + hook-fire counter as the new baseline.
    /// Called from Reset() and from the first Poll() after re-entry.
    void EstablishBaselines() noexcept;

    Callbacks callbacks_;

    // Gate flag — written from the owner's focus path (typically hook
    // thread); read from Poll() (typically MainThreadWorker thread).
    // Acquire-release pairing keeps the gate effects visible.
    std::atomic<bool> chromiumClassActive_{false};

    // Baseline-reset request, latched by SetChromiumClassActive on a
    // true→false edge (hook thread) and consumed by Poll() (worker thread).
    // The invalidation MUST go through this atomic rather than writing the
    // non-atomic poll-state directly: Poll() only re-checks the gate at its
    // ENTRY, so a hook-thread write to accumulatedDrift_/pendingVkCount_ while
    // Poll() is mid-loop would be a data race. Latching keeps all non-atomic
    // mutation single-threaded on the Poll side (honors the contract below).
    std::atomic<bool> pendingReset_{false};

    // Polling-side state — accessed only from Poll() / EstablishBaselines (and
    // the unused public Reset()); single-threaded on the Poll/worker side. The
    // true→false invalidation is routed through pendingReset_ above so it is
    // NOT touched cross-thread. No further sync needed.
    static constexpr size_t kPendingVkCap = 16;
    uint8_t  prevState_[256]                 = {};
    uint8_t  pendingVks_[kPendingVkCap]      = {};
    // Modifier snapshot captured for each buffered VK at the poll that detected
    // it (bit0 Shift, bit1 CapsLock-toggle, bit2 Ctrl, bit3 Alt). Used at replay
    // so each ghost key translates with ITS OWN modifier state, not the trigger-
    // time stateNow (Shift may have been released across polls) — see Poll().
    uint8_t  pendingMods_[kPendingVkCap]     = {};
    uint64_t prevHookFireCount_              = 0;
    uint64_t observedKeyDowns_               = 0;
    uint64_t accumulatedDrift_               = 0;  // cumulative misses since last decay
    size_t   pendingVkCount_                 = 0;  // length of pendingVks_
    uint32_t lastReinstallTickMs_            = 0;
    bool     baselinesValid_                 = false;
};

} // namespace NextKey
