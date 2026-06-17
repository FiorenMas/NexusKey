// VKey - Hook Lifecycle (Wave 3 PR 3.1, 2026-05-23)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Owns the dedicated WH_KEYBOARD_LL + WH_MOUSE_LL hook thread, the HHOOK
// handles, the mailbox wake-up plumbing, and the start-time handshake CV.
// Extracted from HookEngine to isolate Win32 lifecycle concerns from engine
// business logic — HookEngine still owns ProcessKeyDown / focus / config /
// output, but no longer owns the thread/hooks/mailbox storage.
//
// Why a dedicated thread (preserved verbatim from HookEngine's prior design):
//   * `WH_KEYBOARD_LL` callbacks fire on the installer thread's message pump.
//   * Win10/11 silently unhooks any LL hook whose pump can't service events
//     within `LowLevelHooksTimeout` (≤1000ms, registry-tunable). Sciter
//     rendering / SharedState contention / config reloads on the main UI
//     thread were causing those stalls and getting hooks unhooked under us.
//   * Dedicated thread = no UI work = no stalls = no silent unhook.

#pragma once

#include "HookCommandMailbox.h"

#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace NextKey {

/// `WM_APP_REINSTALL_HOOKS` wParam — labels which trigger fired the reinstall.
/// HookEngine sends these from OnFocusChanged for Chromium / Java top-of-chain
/// priority. Public so HookEngine can pass via PostReinstallHooks().
inline constexpr WPARAM REINSTALL_REASON_CHROMIUM = 0;  // focus / mouse-down — 500ms throttle
inline constexpr WPARAM REINSTALL_REASON_JAVA     = 1;  // focus → Java app    — 500ms throttle
// Anti-Dorion v2 (2026-05-28): HookHijackDetector signals a confirmed bypass
// (polled keydown that the hook didn't see). Detector self-cooldowns internally
// at ~150ms, so the pump can throttle this reason more aggressively without
// risking unhook-gap floods — fast recovery is the whole point of the
// detector path.
inline constexpr WPARAM REINSTALL_REASON_HIJACK   = 2;  // detector confirmed — 100ms throttle

class HookLifecycle {
public:
    /// Invoked from the hook thread on WM_APP_HOOK_COMMAND wake-up. HookEngine
    /// sets this at Start() to its DrainHookCommands implementation.
    using DrainFn = std::function<void()>;

    HookLifecycle();
    ~HookLifecycle();

    HookLifecycle(const HookLifecycle&) = delete;
    HookLifecycle& operator=(const HookLifecycle&) = delete;

    /// Spawn the hook thread, install WH_KEYBOARD_LL + WH_MOUSE_LL, block
    /// until the thread signals ready (5s timeout). Returns false on hook
    /// install failure (keyboardHook_ remains nullptr; thread is joined).
    [[nodiscard]] bool Start(HINSTANCE hInstance,
                              HOOKPROC keyboardProc,
                              HOOKPROC mouseProc,
                              DrainFn drainFn);

    /// Post WM_QUIT to the hook thread, join. Hooks are unhooked from the
    /// hook thread itself (MSDN requirement: unhook on installer thread).
    void Stop();

    /// Hook thread id. Returns 0 before Start completes the handshake or
    /// after Stop. Used by external code that needs to PostThreadMessage
    /// (e.g. HotkeyManager routing matched slots back to the hook thread).
    [[nodiscard]] DWORD ThreadId() const noexcept {
        return threadId_.load(std::memory_order_acquire);
    }

    /// True when the keyboard hook was successfully installed and the
    /// thread is still alive. Mouse hook is best-effort and not reflected
    /// here (Stop is the only path that nulls the keyboard hook).
    [[nodiscard]] bool IsRunning() const noexcept {
        return keyboardHook_ != nullptr;
    }

    /// Shared mailbox for cross-thread command bits (focus changes, config
    /// reload, tick poll, toggle VN). Producers (main / worker / hotkey)
    /// call Post; the hook pump's WM_APP_HOOK_COMMAND handler invokes the
    /// drain callback. The wake-fn is wired internally at thread start.
    [[nodiscard]] HookCommandMailbox& Mailbox() noexcept { return mailbox_; }

    /// Ask the hook pump to unhook + reinstall both LL hooks (Chromium /
    /// Java focus-time top-of-chain priority). Throttled internally by the
    /// pump (`kMinReinstallIntervalMs` = 500ms) so a focus-event burst
    /// doesn't produce a burst of unhook gaps.
    void PostReinstallHooks(WPARAM reason) noexcept;

    /// Ghost-key dispatch — called from the hook pump when a
    /// WM_APP_GHOSTKEY arrives. Wraps `engine_->PushChar` etc. on the
    /// hook thread; preserves single-writer doctrine §12 (engine state
    /// mutated only on hook thread). The HookHijackDetector calls
    /// PostGhostKey from its polling thread; the pump invokes this fn.
    /// Defaults to a no-op when never set (detector disabled).
    using GhostKeyFn = std::function<void(wchar_t)>;
    void SetGhostKeyHandler(GhostKeyFn fn) noexcept { ghostKeyFn_ = std::move(fn); }

    /// Post one recovered ghost character onto the hook thread's message
    /// queue. FIFO-ordered (no coalescing — each call delivers separately,
    /// unlike the mailbox's bit-OR semantics). Safe from any thread.
    /// No-op if the thread isn't running yet.
    void PostGhostKey(wchar_t ch) noexcept;

private:
    void ThreadProc();

    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<bool> ready_{false};
    std::mutex startMutex_;
    std::condition_variable startCv_;

    // Hook handles — written by ThreadProc only (single-writer = installer
    // thread). External observers via IsRunning() see the keyboard hook
    // post-install / post-uninstall via the natural Win32 visibility of
    // SetWindowsHookExW / UnhookWindowsHookEx (no fence needed since the
    // CV handshake establishes happens-before with the main thread reader).
    HHOOK keyboardHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;

    HINSTANCE hInstance_ = nullptr;
    HOOKPROC keyboardProc_ = nullptr;
    HOOKPROC mouseProc_ = nullptr;

    HookCommandMailbox mailbox_;
    DrainFn drainFn_;
    GhostKeyFn ghostKeyFn_;
};

}  // namespace NextKey
