// VKey - Hotkey Manager
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "core/config/TypingConfig.h"
#include <Windows.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace NextKey {

/// Hook-thread routing target for HotkeyManager LL-callback matches. HotkeyManager
/// produces it (PostThreadMessageW from LL callback) and HookEngine's pump consumes
/// it (calls DispatchHotkeyFromHookThread). Declared here so both TUs share one
/// definition — the prior pattern (one `static constexpr` per TU) drifted easily.
inline constexpr UINT WM_APP_HOTKEY_FIRED = WM_APP + 3;

/// Multi-slot keyboard hotkey manager. Each slot holds a HotkeyConfig + callback.
/// VKey is a single-layout TIP on English — no layout switching needed.
///
/// Implemented via WH_KEYBOARD_LL (no RegisterHotKey) so we can:
///   1. Eat DOWN/UP of the target key to prevent double-activation.
///   2. Suppress auto-repeat while the combo is held.
///   3. Inject a dummy key tagged with VKEY_EXTRA_INFO to break Windows
///      "Alt/Win tapped alone" detection (browser menu activation bug).
///
/// Wave 1 (2026-05-23) — RCU + cross-thread dispatch:
///   * Slot bindings (config + callback + thread-affinity flag) live in an
///     RCU-published shared_ptr<vector>. The LL hook callback reads lock-free;
///     mutators (AddHotkey / UpdateHotkey / Uninstall) publish new snapshots
///     under `mutationMutex_`.
///   * `comboKeyDown` moves to a parallel `slotState_` vector. Single-writer
///     (LL thread) after Initialize() — no lock needed on the LL hot path.
///   * Matched slots route via PostThreadMessage(hookThreadId_,
///     WM_APP_HOTKEY_FIRED, slotId) → HookEngine pump invokes
///     DispatchHotkeyFromHookThread on the hook thread, restoring the
///     single-writer invariant for callbacks like `hookEngine.CommitPending()`
///     that touch engine state.
///
/// Wave 3 PR 3.7 (2026-05-24) — tag-based protection per
/// `docs/CODING_RULES/12-worker-thread-doctrine.md`:
///   * `Initialize()` REQUIRES the dispatch thread id at construction time
///     — no more late-binding `SetHookThreadId()`. The race window where the
///     LL hook was installed before the tid was published is now structurally
///     impossible.
///   * Each `AddHotkey()` declares `runsOnAnyThread` for its callback:
///       true  = callback is safe on any thread (e.g. lambda body is just
///               `PostMessageW` — Win32 cross-thread-safe). If hookTid is
///               unavailable, the LL callback may invoke it inline.
///       false = callback has a thread-affinity requirement (e.g. mutates
///               `engine_`, which is hook-thread single-writer per Rule
///               11.3). If hookTid is unavailable, the dispatch is dropped
///               — refusing to invoke on the wrong thread is safer than
///               silent UB on `engine_` from the LL thread.
///   * The `else if (slot.callback) slot.callback()` "fallback" from
///     pre-3.7 was a fake protection: it ran ALL callbacks inline including
///     ones that mutated `engine_`. Tag-based gating turns it into real
///     protection — only callbacks that declared `runsOnAnyThread=true`
///     reach the inline path.
class HotkeyManager {
public:
    using Callback = std::function<void()>;
    using SlotId = size_t;

    HotkeyManager() : bindings_(std::make_shared<std::vector<SlotBinding>>()) {}
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    /// Register a hotkey slot. MUST be called before Initialize() — AddHotkey
    /// resizes slotState_, which races with the LL callback if the hook is
    /// already installed (asserts when keyboardHook_ != nullptr). Returns the
    /// slot id for later UpdateHotkey calls.
    ///
    /// `runsOnAnyThread` (Wave 3 PR 3.7): the caller's contract about the
    /// callback's thread affinity. Set TRUE iff the callback is correct
    /// when invoked from any thread (typical body: `PostMessageW` to a
    /// window owned elsewhere — Win32 cross-thread-safe, no shared-state
    /// writes). Set FALSE iff the callback mutates state with a single-
    /// writer requirement (e.g. `hookEngine.CommitPending()` touches
    /// `engine_`, which Rule 11.3 pins to the hook thread). The flag
    /// gates the LL-thread inline-dispatch fallback used when the hook
    /// thread is unavailable.
    [[nodiscard]] SlotId AddHotkey(const HotkeyConfig& config,
                                    Callback callback,
                                    bool runsOnAnyThread);

    /// Replace an existing slot's config (used on config reload). Safe to
    /// call from any thread; serialized by mutationMutex_.
    void UpdateHotkey(SlotId slot, const HotkeyConfig& config);

    /// Install the LL keyboard hook. All AddHotkey() calls must happen first.
    ///
    /// `hookThreadId` is the target for `PostThreadMessage(WM_APP_HOTKEY_FIRED)`
    /// when a hotkey matches. In hook mode pass `hookEngine.GetHookThreadId()`
    /// AFTER HookEngine::Start() has returned (the id is only valid then).
    /// Passing 0 disables cross-thread dispatch — only callbacks that
    /// declared `runsOnAnyThread=true` will fire (others drop silently
    /// rather than violate Rule 11.3). Wave 3 PR 3.7 collapsed the prior
    /// two-step `SetHookThreadId()` + `Initialize()` contract into this
    /// single call so the race window between the two is structurally gone.
    void Initialize(HINSTANCE hInstance, DWORD hookThreadId);

    /// Unhook and clear slots.
    void Uninstall();

    /// Reconcile modifier keys state after desktop switch / Win+L lock.
    /// Uses GetAsyncKeyState to verify physical state and resets stuck modifiers.
    void ReconcileModifiers() noexcept;

    /// Hook-thread dispatch entry point. Called by HookEngine's pump on receipt
    /// of WM_APP_HOTKEY_FIRED. Loads the binding snapshot via RCU and invokes
    /// the per-slot callback. Public + static so the pump can reach it without
    /// holding a HotkeyManager pointer (uses s_instance).
    static void DispatchHotkeyFromHookThread(SlotId slot);

private:
    struct SlotBinding {
        HotkeyConfig config{};
        Callback callback;
        /// Wave 3 PR 3.7 — caller's declaration about the callback's
        /// thread affinity. See AddHotkey() doc above; used to gate the
        /// LL-thread inline-dispatch fallback in the LL hook callback.
        bool runsOnAnyThread = false;
    };
    struct SlotState {
        bool comboKeyDown = false;
    };

    void InstallKeyboardHook(HINSTANCE hInstance);
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static void InjectDummyKey() noexcept;

    // RCU-published binding snapshot. The LL hook callback reads via
    // atomic load (~5 ns, lock-free). Mutators publish a new shared_ptr
    // under `mutationMutex_`; the pointee vector is treated as immutable
    // after publish. Initialized lazily in AddHotkey / Uninstall.
    std::atomic<std::shared_ptr<std::vector<SlotBinding>>> bindings_;

    // Per-slot transient state. LL thread is the only writer; mutators only
    // grow this vector (in AddHotkey, under mutationMutex_) BEFORE Initialize()
    // installs the LL hook, so no read race exists. UpdateHotkey doesn't touch
    // it (preserves comboKeyDown across config reloads).
    std::vector<SlotState> slotState_;

    // Serializes mutators (AddHotkey/UpdateHotkey/Uninstall) with each other.
    // LL callback NEVER acquires this — uses atomic load on bindings_ instead.
    std::mutex mutationMutex_;

    // Wave 3 PR 3.7 — set by Initialize() at install time, never mutated
    // after (LL hook becomes installed in the same Initialize() call, so
    // the LL callback never observes a transition from 0 → non-zero).
    // Reads by the LL callback. Value of 0 means "no cross-thread dispatch
    // target available" → only `runsOnAnyThread=true` slots dispatch.
    std::atomic<DWORD> hookThreadId_{0};

    HHOOK keyboardHook_ = nullptr;

    // Modifier tracking — accessed cross-thread. Main thread reads/writes them
    // in LowLevelKeyboardProc, and HookEngine's thread writes them via ReconcileModifiers.
    std::atomic<bool> modCtrlDown_ = false;
    std::atomic<bool> modShiftDown_ = false;
    std::atomic<bool> modAltDown_ = false;
    std::atomic<bool> modWinDown_ = false;
    std::atomic<bool> otherKeyPressed_ = false;

    static std::atomic<HotkeyManager*> s_instance;
};

}  // namespace NextKey
