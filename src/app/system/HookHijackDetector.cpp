// VKey - Hook Hijack Detector Implementation (Anti-Dorion v2, 2026-05-28)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Linux-portable: no <Windows.h>. Platform interactions (GetKeyboardState,
// ToUnicodeEx) come in via Callbacks — production wires them in HookEngine
// where Windows.h is naturally available; tests provide deterministic
// mocks. Cooldown timing uses std::chrono::steady_clock instead of
// GetTickCount() for the same reason.
//
// 2026-05-28 follow-up — cumulative drift.
//   The original per-poll comparison (`thisPollDowns > hookDelta +
//   tolerance` → trigger) absorbed every realistic Vietnamese-typing
//   bypass: a Dorion-eaten keystroke produced drift=1 per poll, never
//   exceeding tolerance within a single cycle, and drift state didn't
//   carry across polls. Cumulative drift fixes this by maintaining
//   `accumulatedDrift_` across polls and decaying it when the hook
//   counter catches up (race absorption); `pendingVks_` collects missed
//   VKs spanning the polls leading up to the trigger so the ghost-inject
//   recovers the whole sequence, not just the last poll's keys.

#include "HookHijackDetector.h"

#include <chrono>

namespace NextKey {

namespace {

// Per-poll cadence assumed by the owner. The detector does NOT enforce
// this — it's a contract: owner calls Poll at this cadence while
// chromium-fg, slower otherwise. Documented here for the cooldown math.
constexpr uint32_t kAssumedPollIntervalMs = 40;

// After a reinstall is triggered, suppress further drift evaluation for
// this long. Gives the freshly-reinstalled hook a chance to catch up with
// physical keys before the next drift check would otherwise re-fire.
// Sized > assumed poll interval so at least one full poll passes before
// re-evaluation.
constexpr uint32_t kReinstallCooldownMs = 150;

// Drift tolerance — absorbs a 1-key read-order race between the hook's
// fetch_add and our hook-count snapshot (poll reads counter before the
// hook bump completes for the same physical key). With cumulative
// semantics, `accumulatedDrift_ > kDriftTolerance` is the trigger
// predicate: two confirmed misses (or one sustained miss the hook never
// catches up on) escalate past the threshold, while a transient race
// decays out before reaching it.
constexpr uint64_t kDriftTolerance = 1;

// Win32 VK constants used by IsTrackableVk — defined locally so the
// detector source is Linux-buildable without <Windows.h>. Values match
// Win32 winuser.h.
constexpr uint8_t kVkBack    = 0x08;  // VK_BACK
constexpr uint8_t kVkSpace   = 0x20;  // VK_SPACE
constexpr uint8_t kVkOem1    = 0xBA;  // VK_OEM_1 ;:
constexpr uint8_t kVkOem8    = 0xDF;  // VK_OEM_8 — through VK_OEM_2..7
constexpr uint8_t kVkShift   = 0x10;  // VK_SHIFT
constexpr uint8_t kVkControl = 0x11;  // VK_CONTROL
constexpr uint8_t kVkMenu    = 0x12;  // VK_MENU (Alt)
constexpr uint8_t kVkCapital = 0x14;  // VK_CAPITAL (CapsLock)

// Trackable virtual-key codes — alpha A-Z, digits 0-9, space, backspace,
// and the OEM punctuation range the engine processes. Modifier-only keys
// (Shift/Ctrl/Alt/Win/CapsLock), function keys, arrows, etc. are skipped:
// the hook bumps for them, but the polled side filtering them keeps drift
// one-sided — only physical alpha-like activity counts toward bypass
// detection, which is exactly what triggers Vietnamese transformation.
[[nodiscard]] constexpr bool IsTrackableVk(uint8_t vk) noexcept {
    if (vk >= 'A' && vk <= 'Z') return true;
    if (vk >= '0' && vk <= '9') return true;
    if (vk == kVkSpace)         return true;
    if (vk == kVkBack)          return true;
    if (vk >= kVkOem1 && vk <= kVkOem8) return true;
    return false;
}

// GetKeyboardState semantics: high bit (0x80) of each byte is the "down"
// flag. Low bit is toggle state (CapsLock etc.) — ignored.
[[nodiscard]] constexpr bool IsKeyDownByte(uint8_t b) noexcept {
    return (b & 0x80) != 0;
}

// Pack the case-relevant modifier state out of a GetKeyboardState buffer into
// one byte, stored per buffered VK so replay re-translates with the modifiers
// that were live WHEN the key was pressed (not the trigger-time snapshot).
[[nodiscard]] uint8_t PackMods(const uint8_t (&state)[256]) noexcept {
    return static_cast<uint8_t>(
        (IsKeyDownByte(state[kVkShift])   ? 0x01 : 0) |
        ((state[kVkCapital] & 0x01)       ? 0x02 : 0) |
        (IsKeyDownByte(state[kVkControl]) ? 0x04 : 0) |
        (IsKeyDownByte(state[kVkMenu])    ? 0x08 : 0));
}

[[nodiscard]] uint32_t NowMs() noexcept {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

HookHijackDetector::HookHijackDetector(Callbacks callbacks) noexcept
    : callbacks_(std::move(callbacks)) {}

void HookHijackDetector::SetChromiumClassActive(bool active) noexcept {
    const bool wasActive = chromiumClassActive_.exchange(active, std::memory_order_acq_rel);
    if (wasActive != active && !active) {
        // True → false (hook thread): request that the NEXT chromium session
        // re-snapshot from a fresh baseline and drop any half-built drift /
        // pending buffer (Invariant 4). We latch an atomic instead of writing
        // the non-atomic poll-state here: Poll() runs on the worker thread and
        // only re-checks the gate at its ENTRY, so it may already be mid-loop
        // mutating accumulatedDrift_ / pendingVkCount_ — writing them from this
        // thread would be a data race. Poll() consumes the latch on its next
        // entry, keeping all non-atomic mutation single-threaded on the Poll side.
        pendingReset_.store(true, std::memory_order_release);
    }
}

void HookHijackDetector::Reset() noexcept {
    // Explicit baseline re-arm: also drops any accumulated drift / pending
    // VKs. Callers use this on a fresh session boundary where carrying
    // half-built state across would mis-attribute misses.
    baselinesValid_ = false;
    accumulatedDrift_ = 0;
    pendingVkCount_ = 0;
}

void HookHijackDetector::Poll() noexcept {
    // Per-poll gate (Invariant 3): focus may have flipped to a non-chromium
    // app between the owner deciding to call Poll() and now (cross-thread
    // race window). A stale call leaking work while VKey's own dialog is
    // foreground would inject ghost keys into VKey's engine state — the
    // exact v1 RIDEV_INPUTSINK failure mode. Gate guards against that.
    if (!chromiumClassActive_.load(std::memory_order_acquire)) return;

    // Consume a true→false→true reset latched by SetChromiumClassActive
    // (Invariant 4 — fresh baseline per chromium session). Done HERE, on the
    // Poll/worker thread, so the non-atomic poll-state is never mutated cross-
    // thread. Falls through to the EstablishBaselines path below.
    if (pendingReset_.exchange(false, std::memory_order_acquire)) {
        baselinesValid_ = false;
        accumulatedDrift_ = 0;
        pendingVkCount_ = 0;
    }

    if (!baselinesValid_) {
        EstablishBaselines();
        // First poll after re-entry just establishes — no drift yet.
        return;
    }

    // Post-reinstall cooldown — drift will temporarily appear high until
    // the new hook starts firing for fresh keys. Suppress drift evaluation
    // briefly to avoid ping-ponging reinstall on every poll.
    const uint32_t nowMs = NowMs();
    if (lastReinstallTickMs_ != 0 &&
        (nowMs - lastReinstallTickMs_) < kReinstallCooldownMs) {
        return;
    }

    uint8_t stateNow[256] = {};
    if (!callbacks_.readKeyboardState(stateNow)) {
        // Transient read failure — skip this cycle, no baseline update.
        return;
    }

    // Diff vs prevState_: count up→down transitions for trackable VKs,
    // recording up to kPendingVkCap for potential ghost injection.
    uint8_t thisPollVks[kPendingVkCap] = {};
    size_t  thisPollVkCount = 0;
    uint64_t thisPollDowns = 0;
    for (uint16_t vk = 0; vk < 256; ++vk) {
        if (!IsTrackableVk(static_cast<uint8_t>(vk))) continue;
        const bool wasDown = IsKeyDownByte(prevState_[vk]);
        const bool isDown  = IsKeyDownByte(stateNow[vk]);
        if (!wasDown && isDown) {
            ++thisPollDowns;
            if (thisPollVkCount < kPendingVkCap) {
                thisPollVks[thisPollVkCount++] = static_cast<uint8_t>(vk);
            }
        }
    }

    // Advance the polled-state snapshot regardless of drift decision.
    for (uint16_t i = 0; i < 256; ++i) prevState_[i] = stateNow[i];

    const uint64_t hookCountNow   = callbacks_.readHookFireCount();
    const uint64_t hookCountDelta = hookCountNow - prevHookFireCount_;
    prevHookFireCount_ = hookCountNow;

    if (thisPollDowns > hookCountDelta) {
        // More keys polled than the hook saw — drift this cycle.
        const uint64_t newDrift = thisPollDowns - hookCountDelta;
        accumulatedDrift_ += newDrift;

        // Buffer VKs for the eventual ghost-inject. Pure bypass case
        // (hookCountDelta == 0) — every polled VK is a miss, append all.
        // Partial case (hookCountDelta > 0) — the hook saw some, but we
        // can't tell which polled VKs; track drift toward trigger without
        // polluting the inject buffer to avoid double-typing.
        if (hookCountDelta == 0) {
            // Capture this poll's modifier state once — every VK detected in
            // this poll shares it. Stored per entry so replay translates each
            // buffered key with the modifiers live when it was pressed.
            const uint8_t pollMods = PackMods(stateNow);
            for (size_t i = 0;
                 i < thisPollVkCount && pendingVkCount_ < kPendingVkCap;
                 ++i) {
                pendingMods_[pendingVkCount_] = pollMods;
                pendingVks_[pendingVkCount_]  = thisPollVks[i];
                ++pendingVkCount_;
            }
        }
    } else if (hookCountDelta > thisPollDowns) {
        // Hook ahead of poll — either the race the tolerance is built for
        // (hook fired between baseline-sync and state-read) or keyboard
        // auto-repeat firing the hook for a key we already counted on its
        // initial press. Decay accumulated drift; if drift drops to zero,
        // the buffered VKs were "covered" by the hook eventually — drop
        // them so a future trigger doesn't double-inject stale keys.
        observedKeyDowns_ = observedKeyDowns_;  // touch to silence -Wunused (kept for future use)
        const uint64_t overshoot = hookCountDelta - thisPollDowns;
        if (accumulatedDrift_ > overshoot) {
            accumulatedDrift_ -= overshoot;
        } else {
            accumulatedDrift_ = 0;
            pendingVkCount_ = 0;
        }
    }
    // hookCountDelta == thisPollDowns → balanced, no drift change.

    if (accumulatedDrift_ <= kDriftTolerance) return;

    // Bypass confirmed. Inject every buffered ghost-key in observed
    // order, then request reinstall. The receiver only advances engine
    // state (no SendInput) — detector is the safety net behind burst
    // reinstall; on the rare path where it fires, accepting 1-2 raw
    // keys visible to the user is the same trade EVKey makes. Owner
    // routes both through the hook thread (single-writer §12).
    for (size_t i = 0; i < pendingVkCount_; ++i) {
        // Rebuild the modifier state captured when THIS key was buffered, not
        // the trigger-time stateNow (Shift/Caps may have changed across the
        // polls that accumulated the buffer). ToUnicodeEx reads only the VK_*
        // modifier slots from the array, so a minimal reconstruction suffices.
        uint8_t keyState[256] = {};
        const uint8_t m = pendingMods_[i];
        if (m & 0x01) keyState[kVkShift]   = 0x80;
        if (m & 0x02) keyState[kVkCapital] = 0x01;
        if (m & 0x04) keyState[kVkControl] = 0x80;
        if (m & 0x08) keyState[kVkMenu]    = 0x80;
        const wchar_t ch = callbacks_.translateVkToChar(pendingVks_[i], keyState);
        if (ch != 0) callbacks_.injectGhostChar(ch);
    }
    callbacks_.requestReinstall();
    lastReinstallTickMs_ = nowMs;
    accumulatedDrift_ = 0;
    pendingVkCount_ = 0;
}

void HookHijackDetector::EstablishBaselines() noexcept {
    if (!callbacks_.readKeyboardState(prevState_)) {
        // Zero the snapshot so the first real poll doesn't false-positive
        // on "everything just transitioned down". Read failure on baseline
        // capture is rare (transient kernel issue).
        for (auto& b : prevState_) b = 0;
    }
    prevHookFireCount_ = callbacks_.readHookFireCount();
    observedKeyDowns_ = 0;
    accumulatedDrift_ = 0;
    pendingVkCount_ = 0;
    lastReinstallTickMs_ = 0;
    baselinesValid_ = true;
}

}  // namespace NextKey
