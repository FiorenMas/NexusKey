// VKey - Hook command mailbox (Phase 2a — single-writer composition state)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Phase 2 of the 2026-05-19 architecture review design
// (docs/plans/2026-05-19-architecture-review-design.md §Phase 2).
//
// Cross-thread channel that lets non-hook threads (main UI thread,
// MainThreadWorker tick, tray/hotkey callbacks) request work on the hook
// thread WITHOUT mutating composition state directly. Producers post a
// bit flag (and optionally a `FocusClassification` snapshot for focus
// changes); the hook thread drains in its LL callback before processing
// the keystroke.
//
// Design highlights (see plan §Phase 2):
//   - Atomic bit-OR coalescing — N posts of the same bit collapse to one.
//   - shared_ptr "later wins" for pendingFocus — only the most recent
//     focus snapshot matters; intermediate transient HWNDs are discarded.
//   - wakePosted edge latch — a burst of posts collapses to a single
//     PostThreadMessage in production (avoids waking the hook pump every
//     time the user moves the mouse over a tray window).
//   - Drain ordering: `wakePosted=false` MUST happen BEFORE
//     `bits.exchange(0)`. This is the most subtle invariant in the
//     design; HookCommandMailboxTest §DrainClearsWakeLatchBeforeReturning
//     locks the visible behaviour, the inline comment in `DrainBits()`
//     locks the implementation.
//
// Linux-portable: no Win32 primitives in this header. The wake function
// (`WakeFn`) is injected at `HookEngine::Start` with a PostThreadMessage
// trampoline; tests inject a counter or leave it unset.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace NextKey {

/// Bit flags for `HookCommandMailbox::Post` / `DrainBits` return value.
/// One bit per command type — drain dispatches by mask test. Bits stay in
/// a uint32_t for std::atomic lock-free guarantee on every supported
/// platform (Rule 11.3).
namespace HookCommand {
    constexpr std::uint32_t kFocusChanged = 0x01;  // pendingFocus carries new classification
    constexpr std::uint32_t kConfigApply  = 0x02;  // Settings save / TOML reload requested
    constexpr std::uint32_t kTickPoll     = 0x04;  // 200ms MainThreadWorker tick (CJK layout etc.)
    constexpr std::uint32_t kToggleVN     = 0x08;  // hotkey / tray toggled V/E mode
}

/// Snapshot of all the data the hook thread needs to react to a focus
/// change. Produced on the main thread (WinEventProc) via the pure
/// `ClassifyFocusedWindow` extracted in Phase 2b; consumed on the hook
/// thread inside the drain. `hwndOpaque` is `HWND` cast to uintptr_t so
/// this header stays free of <Windows.h>.
///
/// All fields are POD-ish; the struct is held as `std::shared_ptr<const>`
/// once posted so a future Post that arrives before the hook thread drains
/// can atomically swap a newer snapshot in without copying.
struct FocusClassification {
    std::uintptr_t hwndOpaque{0};
    std::uint32_t  pid{0};        // GetWindowThreadProcessId result; 0 = unknown
    std::wstring   exeName;
    // Classification flags — populated by ClassifyFocusedWindow.
    bool isExcluded{false};
    bool isForcedVietnamese{false};  // per-app hard-V lock (mutually exclusive with isExcluded)
    bool isTsf{false};
    bool isElectron{false};
    bool isConsole{false};
    bool isBrowser{false};
    bool isQtApp{false};
    bool isVB6{false};
    bool isWebView2{false};
    bool isJavaApp{false};
    bool isKnownHijacker{false};
    // Dispatch-shape flags derived from classification + per-app overrides.
    bool localSkipEmpty{false};
    bool localNeedBait{false};
    // True only for spreadsheet hosts (Excel) where a cell starting with '=' is
    // a formula. Gates the formula-segment bait suppression so it never leaks
    // into other needBait hosts (browser omnibox, Outlook) — see
    // core/FormulaSegmentDecision.h and HookEngine::UpdateFormulaSegment.
    bool localFormulaHost{false};
    bool localClipboard{false};
    bool localEditMsg{false};
    bool localForceEmReplaceSel{false};
    bool localUseClipboardInjector{false};
    bool localElectronApp{false};  // (isElectron || isWebView2) && !isConsole
    // Per-app "send method = compatibility split" (AppOverrideEntry::sendMethod
    // 2/3). 0 = not forced; >0 = inter-batch sleep (ms) for SplitDispatchInjector.
    // Resolved from snap->appSendMethodOverrides in ClassifyFocusedWindow; the
    // hook thread copies it into Output::WindowClassification::forcedSplitSleepMs.
    int localForcedSplitSleepMs{0};
    // RESOLVED target values for the focused app. Classify captures the
    // current global on main alongside any per-app override, so the hook
    // thread never reads `globalCodeTable_` / `globalInputMethod_`
    // directly (those are still mutated from main by ApplyConfig /
    // SetCodeTable / ReloadFromToml). Numeric placeholders keep the
    // engine/encoding enums out of this header (Linux-portable);
    // -1 means "classify ran without a window" — Apply early-returns.
    int targetMethod{-1};      // InputMethod enum value
    int targetCodeTable{-1};   // CodeTable enum value
    // True if the trigger HWND failed every visibility/size sanity check
    // and should be classified for dispatch but NOT update currentExe_.
    // Helper events return early in ApplyFocusOnHookThread BEFORE the
    // late PID/exe update; the OnTickPoll defensive PID update is the
    // fallback that catches a real app whose only initial events were
    // helpers (Bug 1 notepad++ launch).
    bool skipAppTracking{false};
};

class HookCommandMailbox {
public:
    /// Wake trampoline — invoked once per "empty → non-empty" transition.
    /// In production wires to `PostThreadMessage(hookThreadId_,
    /// WM_APP_HOOK_COMMAND, 0, 0)`. Tests inject a counter or leave unset.
    using WakeFn = std::function<void()>;

    HookCommandMailbox() = default;
    HookCommandMailbox(const HookCommandMailbox&)            = delete;
    HookCommandMailbox& operator=(const HookCommandMailbox&) = delete;

    void SetWakeFn(WakeFn fn) noexcept { wakeFn_ = std::move(fn); }

    /// Reset the wake latch and pending bits to pristine. Call when a
    /// HookLifecycle is stopped so a stale `wakePosted_` (a Post whose
    /// WM_APP_HOOK_COMMAND the pump exited before draining) cannot suppress the
    /// FIRST Post's wake after a subsequent Start — otherwise that command sits
    /// stranded until an unrelated keydown drains it. No-op in the normal
    /// start-once production flow; matters for restart / test-reuse paths.
    void ResetLatch() noexcept {
        bits_.store(0, std::memory_order_relaxed);
        wakePosted_.store(false, std::memory_order_relaxed);
    }

    /// Post a command bit (and optional focus snapshot). Lock-free, safe
    /// from any thread except the hook thread itself. Fires the wake
    /// trampoline exactly once per empty→non-empty transition.
    void Post(std::uint32_t bit,
              std::shared_ptr<const FocusClassification> cls = nullptr) noexcept;

    /// Drain accumulated bits and clear them. Returns the OR of every
    /// `Post(bit, ...)` call since the previous drain. Caller dispatches
    /// based on the returned mask and calls `ConsumePendingFocus()` iff
    /// `kFocusChanged` is set.
    ///
    /// CONTRACT (design §Critical ordering rule): `wakePosted_` clears
    /// BEFORE `bits_` exchanges. Producers racing with drain will see
    /// `wakePosted_==false` and fire the wake — no stranded bits.
    [[nodiscard]] std::uint32_t DrainBits() noexcept;

    /// Take ownership of the most recent posted focus snapshot. Subsequent
    /// calls return nullptr until the next `Post(kFocusChanged, cls)`.
    [[nodiscard]] std::shared_ptr<const FocusClassification>
    ConsumePendingFocus() noexcept;

    // ── Test / introspection helpers ────────────────────────────────────
    /// Read bits without draining. Test-only; production code uses Drain.
    [[nodiscard]] std::uint32_t PeekBits() const noexcept {
        return bits_.load(std::memory_order_acquire);
    }

    /// True iff a wake has been signalled and a drain hasn't yet cleared
    /// the latch. Test-only: locks the ordering contract in
    /// `DrainClearsWakeLatchBeforeReturning`.
    [[nodiscard]] bool IsWakePending() const noexcept {
        return wakePosted_.load(std::memory_order_acquire);
    }

    // ── Drain-scope guard (Phase 2d) ────────────────────────────────────
    /// True for the lifetime of a `DrainScope` somewhere. Production
    /// fueling for debug-only `assert(!IsDraining())` checks at forbidden
    /// re-entry points (Phase 4 replay harness will lean on this).
    /// Atomic — readable from any thread without TSAN warnings.
    [[nodiscard]] bool IsDraining() const noexcept {
        return inDrain_.load(std::memory_order_acquire);
    }

    /// RAII helper claimed by `HookEngine::DrainHookCommands` for the
    /// duration of dispatch. Flips `IsDraining()` to true on construction
    /// and back on destruction. Asserts (debug) if a nested scope is
    /// attempted on the same mailbox — that's the "drain handler called
    /// DrainHookCommands recursively" bug pattern.
    class DrainScope {
    public:
        explicit DrainScope(HookCommandMailbox& mb) noexcept;
        ~DrainScope() noexcept;
        DrainScope(const DrainScope&)            = delete;
        DrainScope& operator=(const DrainScope&) = delete;
    private:
        HookCommandMailbox& mailbox_;
    };

private:
    std::atomic<std::uint32_t>                            bits_{0};
    std::atomic<std::shared_ptr<const FocusClassification>> pendingFocus_;
    std::atomic<bool>                                     wakePosted_{false};
    std::atomic<bool>                                     inDrain_{false};
    WakeFn                                                wakeFn_;
};

}  // namespace NextKey
