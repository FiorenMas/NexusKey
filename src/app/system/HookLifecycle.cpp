// VKey - Hook Lifecycle Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "HookLifecycle.h"
#include "HotkeyManager.h"  // WM_APP_HOTKEY_FIRED + DispatchHotkeyFromHookThread
#include "core/CrashLog.h"
#include "core/Debug.h"

namespace NextKey {

// Mirrors the HookEngine.cpp prefix-only pattern: args not evaluated when the
// runtime logger is off, so HOOK_LOG in tight pump loops is near-zero cost.
#define HOOK_LIFE_LOG(fmt, ...) do {                                           \
    if (::NextKey::Logger::IsEnabled())                                        \
        ::NextKey::Logger::Log(L"[HookLife] " fmt, ##__VA_ARGS__);             \
} while (0)

// Custom WM_APP message ids used inside this pump. WM_APP_HOTKEY_FIRED lives
// in HotkeyManager.h (shared with HotkeyManager's LL callback that posts it).
static constexpr UINT WM_APP_REINSTALL_HOOKS = WM_APP + 1;
static constexpr UINT WM_APP_HOOK_COMMAND    = WM_APP + 2;
// WM_APP + 3 = WM_APP_HOTKEY_FIRED (HotkeyManager.h).
// WM_APP + 4 = ghost-key recovery from HookHijackDetector (Anti-Dorion v2).
// wParam carries the recovered wchar_t; lParam unused. FIFO-ordered: each
// PostThreadMessage delivers separately (no coalescing — required for the
// "inject N missed keys in observed order" contract).
static constexpr UINT WM_APP_GHOSTKEY        = WM_APP + 4;

HookLifecycle::HookLifecycle() = default;

HookLifecycle::~HookLifecycle() {
    Stop();
}

bool HookLifecycle::Start(HINSTANCE hInstance,
                           HOOKPROC keyboardProc,
                           HOOKPROC mouseProc,
                           DrainFn drainFn) {
    if (keyboardHook_) return false;  // Already running

    hInstance_     = hInstance;
    keyboardProc_  = keyboardProc;
    mouseProc_     = mouseProc;
    drainFn_       = std::move(drainFn);
    ready_.store(false, std::memory_order_release);

    thread_ = std::thread(&HookLifecycle::ThreadProc, this);

    // Wait for hook thread to finish installing hooks (or fail). Timeout 5s
    // as safety — a healthy thread signals within milliseconds.
    {
        std::unique_lock<std::mutex> lk(startMutex_);
        startCv_.wait_for(lk, std::chrono::seconds(5),
                          [this] { return ready_.load(std::memory_order_acquire); });
    }
    if (!keyboardHook_) {
        NEXTKEY_LOG(L"HookLifecycle: keyboard hook install failed (thread did not signal ready or SetWindowsHookExW failed)");
        if (thread_.joinable()) {
            const DWORD tid = threadId_.load(std::memory_order_acquire);
            if (tid) PostThreadMessage(tid, WM_QUIT, 0, 0);
            thread_.join();
        }
        return false;
    }
    return true;
}

void HookLifecycle::Stop() {
    if (thread_.joinable()) {
        const DWORD tid = threadId_.load(std::memory_order_acquire);
        if (tid) PostThreadMessage(tid, WM_QUIT, 0, 0);
        thread_.join();
    }
    keyboardHook_ = nullptr;
    mouseHook_    = nullptr;
    threadId_.store(0, std::memory_order_release);
    ready_.store(false, std::memory_order_release);
    drainFn_ = nullptr;
    ghostKeyFn_ = nullptr;
    // #10: clear any stale wake latch / pending bits so that if this lifecycle
    // is restarted, the first cross-thread Post fires its wake instead of being
    // suppressed by a wakePosted_=true left over from a command the pump exited
    // before draining. No-op in the normal start-once flow.
    mailbox_.ResetLatch();
}

void HookLifecycle::PostReinstallHooks(WPARAM reason) noexcept {
    const DWORD tid = threadId_.load(std::memory_order_acquire);
    if (tid) PostThreadMessageW(tid, WM_APP_REINSTALL_HOOKS, reason, 0);
}

void HookLifecycle::PostGhostKey(wchar_t ch) noexcept {
    const DWORD tid = threadId_.load(std::memory_order_acquire);
    // wParam is WPARAM (uintptr_t-sized); wchar_t fits in the low 16 bits.
    if (tid) PostThreadMessageW(tid, WM_APP_GHOSTKEY,
                                 static_cast<WPARAM>(ch), 0);
}

void HookLifecycle::ThreadProc() {
    // Dedicated message-pump thread for WH_KEYBOARD_LL + WH_MOUSE_LL. These
    // are installer-thread-bound — the callback runs on this thread, and
    // Windows dispatches events via the thread's message queue. Keeping
    // this thread otherwise idle guarantees the pump stays responsive
    // within the LowLevelHooksTimeout window (silent-unhook avoidance).
    threadId_.store(GetCurrentThreadId(), std::memory_order_release);

    // Phase 2a: wire the mailbox wake trampoline now that we own a valid
    // thread id. Producers on other threads call mailbox_.Post(...); the
    // first post per empty→non-empty edge fires this lambda which kicks
    // the pump via WM_APP_HOOK_COMMAND. Subsequent posts in the same edge
    // coalesce (wakePosted latch).
    const DWORD wakeTid = threadId_.load(std::memory_order_acquire);
    mailbox_.SetWakeFn([wakeTid]{
        PostThreadMessageW(wakeTid, WM_APP_HOOK_COMMAND, 0, 0);
    });

    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc_, hInstance_, 0);
    if (!keyboardHook_) {
        HOOK_LIFE_LOG(L"ThreadProc: SetWindowsHookExW(WH_KEYBOARD_LL) FAILED err=%lu", GetLastError());
        // Signal Start() that we tried (but failed) so it can observe
        // keyboardHook_ == nullptr and abort.
        {
            std::lock_guard<std::mutex> lk(startMutex_);
            ready_.store(true, std::memory_order_release);
        }
        startCv_.notify_one();
        return;
    }

    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseProc_, hInstance_, 0);
    // Mouse hook is best-effort — proceed even if it fails.

    // Signal main: hooks installed, HHOOKs visible via keyboardHook_/mouseHook_.
    {
        std::lock_guard<std::mutex> lk(startMutex_);
        ready_.store(true, std::memory_order_release);
    }
    startCv_.notify_one();

    HOOK_LIFE_LOG(L"ThreadProc: pump started tid=%lu", wakeTid);

    // Message pump. Besides LL hook dispatch, this thread services:
    //  • WM_APP_REINSTALL_HOOKS — posted by HookEngine::OnFocusChanged for
    //    Chromium / Java top-of-chain priority. wParam = REINSTALL_REASON_*.
    //    Throttled (`kMinReinstallIntervalMs`) so a focus-event burst doesn't
    //    translate into a burst of unhook/rehook gaps. Each gap is µs-to-ms;
    //    one is harmless but five back-to-back can swallow a stray keystroke.
    //  • WM_APP_HOOK_COMMAND — mailbox wake-up; invokes drainFn_ provided by
    //    HookEngine. Drain is ALSO called from inside LowLevelKeyboardProc
    //    (step 5 barrier in HookEngine), so reaching it here means no
    //    keystroke triggered a drain between the post and this pump cycle.
    //  • WM_APP_HOTKEY_FIRED — HotkeyManager LL callback posted slot id in
    //    wParam (Wave 1). DispatchHotkeyFromHookThread invokes the per-slot
    //    callback in hook-thread context, restoring the single-writer
    //    invariant for callbacks that call hookEngine.CommitPending().
    //  • WM_APP_GHOSTKEY — HookHijackDetector posted a recovered wchar_t
    //    in wParam after observing a key our LL hook didn't see (bypass
    //    by a higher LL hook, Anti-Dorion v2). Invokes ghostKeyFn_ which
    //    typically routes into HookEngine::HandleGhostChar to advance
    //    engine state without emitting SendInput (the app already has
    //    the raw key as physical input). §12 doctrine: engine mutation
    //    lands on this hook thread.
    MSG msg;
    // Anti-Dorion v2 (Pillar 3 — split throttle by reason). Pre-v2 a single
    // 500ms throttle covered all reinstalls, which made the detector path
    // (confirmed bypass — needs prompt recovery) wait behind a recent focus
    // or mouse-down reinstall. Per-reason throttle lets the hijack path
    // recover within ~100ms while focus/mouse keep their 500ms gap that
    // prevents unhook-storms from alt-tab spam or click bursts.
    DWORD lastReinstallTime[3] = {0, 0, 0};  // indexed by REINSTALL_REASON_*
    constexpr DWORD kThrottleMs[3] = {
        500,  // REINSTALL_REASON_CHROMIUM (focus / mouse-down)
        500,  // REINSTALL_REASON_JAVA     (focus → Java)
        100,  // REINSTALL_REASON_HIJACK   (detector confirmed bypass)
    };
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_REINSTALL_HOOKS) {
            // Defensive bounds check — reason is user-controlled (any
            // POST_THREAD_MESSAGE caller) so a stray wParam must not OOB
            // the throttle table. Fall back to the strictest (CHROMIUM)
            // bucket for unknown reasons; ignore the post itself doesn't
            // help — the hook is already wedged when we'd most need it.
            const WPARAM rawReason = msg.wParam;
            const size_t reasonIdx = (rawReason < 3) ? static_cast<size_t>(rawReason)
                                                     : 0;
            const wchar_t* reasonName =
                rawReason == REINSTALL_REASON_JAVA   ? L"java"
              : rawReason == REINSTALL_REASON_HIJACK ? L"hijack"
              : L"chromium";
            const DWORD now = GetTickCount();
            const DWORD sinceLast = now - lastReinstallTime[reasonIdx];
            const DWORD throttle  = kThrottleMs[reasonIdx];
            if (lastReinstallTime[reasonIdx] != 0 && sinceLast < throttle) {
                HOOK_LIFE_LOG(L"ThreadProc: reinstall SKIPPED (throttle %ums < %ums) reason=%ls",
                              sinceLast, throttle, reasonName);
                continue;
            }
            lastReinstallTime[reasonIdx] = now;

            // Unhook before re-install. Don't gate the re-install on the
            // unhook target existing — if a prior reinstall transient-failed
            // and left a NULL handle, we still want to attempt recovery
            // (gating would lock out retry permanently).
            if (keyboardHook_) UnhookWindowsHookEx(keyboardHook_);
            keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc_, hInstance_, 0);
            if (!keyboardHook_) {
                HOOK_LIFE_LOG(L"ThreadProc: SetWindowsHookExW(WH_KEYBOARD_LL) reinstall FAILED err=%lu",
                              GetLastError());
            }

            if (mouseHook_) UnhookWindowsHookEx(mouseHook_);
            mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseProc_, hInstance_, 0);
            if (!mouseHook_) {
                HOOK_LIFE_LOG(L"ThreadProc: SetWindowsHookExW(WH_MOUSE_LL) reinstall FAILED err=%lu",
                              GetLastError());
            }

            HOOK_LIFE_LOG(L"ThreadProc: Hooks reinstalled reason=%ls kb=%ls mouse=%ls",
                          reasonName,
                          keyboardHook_ ? L"OK" : L"FAIL",
                          mouseHook_ ? L"OK" : L"FAIL");
            continue;
        }
        if (msg.message == WM_APP_HOOK_COMMAND) {
            if (drainFn_) {
                try {
                    drainFn_();
                } catch (const std::exception& e) {
                    CrashLog(L"HookLifecycle::DrainCallback", e.what());
                } catch (...) {
                    CrashLog(L"HookLifecycle::DrainCallback", "(non-std exception)");
                }
            }
            continue;
        }
        if (msg.message == WM_APP_HOTKEY_FIRED) {
            try {
                HotkeyManager::DispatchHotkeyFromHookThread(static_cast<size_t>(msg.wParam));
            } catch (const std::exception& e) {
                CrashLog(L"HookLifecycle::DispatchHotkey", e.what());
            } catch (...) {
                CrashLog(L"HookLifecycle::DispatchHotkey", "(non-std exception)");
            }
            continue;
        }
        if (msg.message == WM_APP_GHOSTKEY) {
            // Ghost-key dispatch (Anti-Dorion v2): the HookHijackDetector
            // observed a key the hook didn't see (we were bypassed); the
            // recovered wchar_t arrives in wParam. Invoke the handler on
            // this hook thread — §12 single-writer doctrine for engine
            // mutations. Handler may be unset if no detector is wired
            // (test builds, future config disable) → no-op safely.
            if (ghostKeyFn_) {
                try {
                    ghostKeyFn_(static_cast<wchar_t>(msg.wParam));
                } catch (const std::exception& e) {
                    CrashLog(L"HookLifecycle::DispatchGhostKey", e.what());
                } catch (...) {
                    CrashLog(L"HookLifecycle::DispatchGhostKey", "(non-std exception)");
                }
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Must unhook on the same thread that installed (MSDN requirement).
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    HOOK_LIFE_LOG(L"ThreadProc: pump exited tid=%lu", wakeTid);
}

}  // namespace NextKey
