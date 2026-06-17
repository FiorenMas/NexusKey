// VKey - Focus Owner (Wave 3 PR 3.2, 2026-05-24)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Owns the focus-tracking subsystem extracted from HookEngine:
//   * WinEvent hooks (EVENT_SYSTEM_FOREGROUND + EVENT_SYSTEM_MINIMIZEEND)
//   * Foreground PID tracker, current/previous exe names
//   * Focused-child HWND + class cache (used by VB6/ANSI EM_REPLACESEL path)
//   * Per-HWND classification LRU cache (AppProfile)
//   * Smart-switch per-app mode map + persistence
//   * CJK layout state (suppressed/modeBeforeCjk/cachedIsCompatLayout)
//   * WebView2 positive-only detection cache
//   * Window classification helpers (ClassifyFocusedWindow + ClassifyWindow)
//
// HookEngine still owns the *reactions* to focus events
// (ApplyFocusOnHookThread / OnLayoutChanged / OnFocusChanged orchestration);
// FocusOwner holds the state and the classification heavy work, and notifies
// HookEngine via the `FocusChangedFn` callback registered at `Install()`.
//
// Threading:
//   * WinEventProc fires on the WinEvent installer thread (main).
//   * State mutators (currentExe_, layoutSuppressed_, appProfileCache_,
//     appModeMap_, cachedFocusedClass_) are written on the hook thread from
//     ApplyFocusOnHookThread / OnTickPoll → drain, never from WinEventProc.
//   * cachedFocusedHwnd_ is atomic — read on key thread, written from hook
//     thread (RefreshFocusCache) and mouse-hook thread (InvalidateFocusCache).
//   * lastForegroundPid_ is atomic — written on hook thread, read on worker
//     thread inside OnTickPoll (PID-changed fallback).

#pragma once

#include "core/config/TypingConfig.h"
#include "core/config/ConfigSnapshot.h"
#include "core/SmartSwitchManager.h"
#include "app/system/HookCommandMailbox.h"  // FocusClassification

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace NextKey {

class FocusOwner {
public:
    /// Fired from WinEventProc (main thread) on FOREGROUND + MINIMIZEEND.
    /// HookEngine wires this at Install() to its OnFocusChanged shim
    /// (QuickSync + Classify + Post(kFocusChanged)).
    using FocusChangedFn = std::function<void(HWND triggerHwnd)>;

    /// Fired from Classify() when the focused app is Chromium/Java and we
    /// want to re-install the LL hooks at the top of the chain. HookEngine
    /// wires this to `lifecycle_.PostReinstallHooks(reason)`, gated on
    /// `lifecycle_.ThreadId()` so FocusOwner doesn't need to know about
    /// HookLifecycle.
    using ReinstallFn = std::function<void(WPARAM reason)>;

    /// Per-HWND classification cache entry. PID re-validated on lookup to
    /// catch HWND reuse after a process dies. Bounded LRU eviction at
    /// `kMaxAppProfileCache` entries.
    struct AppProfile {
        DWORD pid = 0;
        std::wstring exeName;       // lowercase exe name (matches override-map keys)
        bool isBrowser  = false;
        bool isElectron = false;
        bool isQtApp    = false;
        bool isConsole  = false;
        bool isVB6      = false;
        bool isWebView2 = false;
        uint64_t cachedAt = 0;      // GetTickCount64() — LRU evict timestamp
    };

    /// Read-only inputs Classify() needs from HookEngine's RCU snapshots.
    /// Built by HookEngine at each call so FocusOwner never holds engine
    /// state pointers.
    struct ConfigContext {
        std::shared_ptr<const TypingConfig>   cfg;
        std::shared_ptr<const ConfigSnapshot> snap;
        int globalCodeTable   = 0;   // CodeTable enum value
        int globalInputMethod = 0;   // InputMethod enum value
    };

    FocusOwner();
    ~FocusOwner();

    FocusOwner(const FocusOwner&) = delete;
    FocusOwner& operator=(const FocusOwner&) = delete;

    /// Install both WinEvent hooks (FOREGROUND + MINIMIZEEND). Returns
    /// true iff the FOREGROUND hook installed (MINIMIZEEND is best-effort).
    /// Sets `s_instance` for the static WinEventProc dispatch.
    [[nodiscard]] bool Install(FocusChangedFn onFocusChanged,
                                ReinstallFn   onReinstall);
    /// Unhook + clear `s_instance`. Idempotent.
    void Uninstall();

    /// Heavy Win32 classifier: cache lookup → ClassifyWindow →
    /// GetExeNameForHwnd → (sometimes) IsWebView2App → override-map
    /// resolution. Called from main (WinEventProc) and worker (OnTickPoll).
    /// Never accesses engine state. Posts a reinstall request via
    /// `onReinstall_` when the focused app is Chromium/Java.
    [[nodiscard]] FocusClassification Classify(HWND triggerHwnd,
                                                const ConfigContext& ctx) noexcept;

    /// Populate cachedFocusedHwnd_/cachedFocusedClass_ via AttachThreadInput.
    /// Called from ApplyFocusOnHookThread (hook thread) when the focused app
    /// needs the EM_REPLACESEL path (VB6/ANSI) or the legacy edit-msg path
    /// (Win11 Notepad).
    void RefreshFocusCache(HWND foreground) noexcept;
    /// Drop the cached HWND/class. Called from LowLevelMouseProc when a
    /// click moves focus within an app (no EVENT_SYSTEM_FOREGROUND fires).
    void InvalidateFocusCache() noexcept;

    [[nodiscard]] HWND CachedFocusedHwnd() const noexcept {
        return cachedFocusedHwnd_.load(std::memory_order_relaxed);
    }
    /// Tuple race with HWND is benign — see header doc on threading.
    [[nodiscard]] const std::wstring& CachedFocusedClass() const noexcept {
        return cachedFocusedClass_;
    }

    // ── Foreground PID tracker ──────────────────────────────────────────
    [[nodiscard]] DWORD LastForegroundPid() const noexcept {
        return lastForegroundPid_.load(std::memory_order_acquire);
    }
    void SetLastForegroundPid(DWORD pid) noexcept {
        lastForegroundPid_.store(pid, std::memory_order_release);
    }

    // ── Active / last-real / previous exe (hook thread only) ───────────
    // Split state model (2026-05-26 refactor):
    //
    //   activeExe_    — tracks the latest focused window's exe, including
    //                   helper windows (dock panels, tooltips, taskbar).
    //                   Used by toggle and per-app overrides so map writes
    //                   always attribute to the app the user is actually
    //                   interacting with — not the last non-helper app.
    //
    //   lastRealExe_  — tracks only non-skipAppTracking transitions. The
    //                   app whose mode the engine state corresponds to.
    //                   SAVE/RESTORE blocks use this so helper-event
    //                   detours don't poison adjacent map entries.
    //                   Auto-shifts previousExe_ for the encoding-override
    //                   fallback chain at HookEngine.cpp:410.
    //
    //   previousExe_  — last lastRealExe_ value before the most recent
    //                   real-app transition. Encoding-override fallback.
    [[nodiscard]] const std::wstring& ActiveExe() const noexcept { return activeExe_; }
    [[nodiscard]] const std::wstring& LastRealExe() const noexcept { return lastRealExe_; }
    [[nodiscard]] const std::wstring& PreviousExe() const noexcept { return previousExe_; }

    void SetActiveExe(std::wstring exe) noexcept { activeExe_ = std::move(exe); }
    void SetLastRealExe(std::wstring exe) noexcept {
        if (!lastRealExe_.empty()) previousExe_ = lastRealExe_;
        lastRealExe_ = std::move(exe);
    }

    // ── Smart-switch state machine (hook thread only) ───────────────────
    [[nodiscard]] std::unordered_map<std::wstring, bool>& AppModeMap() noexcept {
        return appModeMap_;
    }
    [[nodiscard]] const std::unordered_map<std::wstring, bool>& AppModeMap() const noexcept {
        return appModeMap_;
    }
    /// Read/write from BOTH hook thread (Mark/Clear at SAVE/toggle blocks)
    /// AND worker thread (OnTickPoll debounce gate) — must be atomic.
    /// Pairs with the appModesSnap_ acquire/release on the read path so a
    /// worker observing dirty=true is guaranteed to see the snapshot from
    /// the matching MarkAppModeDirty.
    [[nodiscard]] bool AppModeDirty() const noexcept {
        return appModeDirty_.load(std::memory_order_acquire);
    }
    void MarkAppModeDirty() noexcept {
        appModeDirty_.store(true, std::memory_order_release);
    }
    void ClearAppModeDirty() noexcept {
        appModeDirty_.store(false, std::memory_order_release);
    }
    [[nodiscard]] SmartSwitchManager& Smart() noexcept { return smartSwitchMgr_; }
    [[nodiscard]] const SmartSwitchManager& Smart() const noexcept { return smartSwitchMgr_; }

    /// Hook-thread MarkDirty timestamp (GetTickCount64 on Windows; injected
    /// clock in tests). Worker reads to enforce debounce window.
    [[nodiscard]] std::uint64_t LastDirtyTs() const noexcept {
        return lastDirtyTs_.load(std::memory_order_acquire);
    }
    void SetLastDirtyTs(std::uint64_t ts) noexcept {
        lastDirtyTs_.store(ts, std::memory_order_release);
    }

    /// RCU publish: hook thread copies the live `appModeMap_` into a fresh
    /// `shared_ptr<const map>` and atomic-stores it. Cost: 1 alloc + map
    /// copy (~1.5 KB at cap 50). Cold path (focus-change handler), not the
    /// keystroke hot path. Must be called after every mutation to
    /// `appModeMap_`. Pairs with `SnapshotAppModes()` on worker thread.
    void PublishAppModesSnapshot() noexcept;

    /// Worker-thread snapshot load (atomic). Returns null if no snapshot
    /// has ever been published. Caller treats null as "nothing to write".
    [[nodiscard]] std::shared_ptr<const std::unordered_map<std::wstring, bool>>
    SnapshotAppModes() const noexcept;

    // ── CJK layout state machine (hook thread only) ─────────────────────
    [[nodiscard]] bool LayoutSuppressed() const noexcept { return layoutSuppressed_; }
    void SetLayoutSuppressed(bool v) noexcept { layoutSuppressed_ = v; }
    [[nodiscard]] bool ModeBeforeCjk() const noexcept { return modeBeforeCjk_; }
    void SetModeBeforeCjk(bool v) noexcept { modeBeforeCjk_ = v; }
    [[nodiscard]] bool CachedIsCompatLayout() const noexcept { return cachedIsCompatLayout_; }
    void SetCachedIsCompatLayout(bool v) noexcept { cachedIsCompatLayout_ = v; }
    /// Stop-time reset. Mirrors the legacy HookEngine::Stop pair so a
    /// future Start picks up clean layout state.
    void ResetLayoutState() noexcept {
        layoutSuppressed_     = false;
        cachedIsCompatLayout_ = true;
    }

    // ── Exe-name helper (static — pure Win32 lookup, no instance state) ─
    [[nodiscard]] static std::wstring GetExeNameForHwnd(HWND hwnd) noexcept;

    // ── Dynamic hijackers registration & check (thread-safe) ────────────
    void RegisterDynamicHijacker(const std::wstring& exeName) noexcept;
    [[nodiscard]] bool IsDynamicHijacker(const std::wstring& exeName) const noexcept;

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hHook, DWORD event,
                                       HWND hwnd, LONG idObj, LONG idChild,
                                       DWORD tid, DWORD time);

    [[nodiscard]] const AppProfile* LookupAppProfile(HWND hwnd) noexcept;
    void StoreAppProfile(HWND hwnd, AppProfile profile) noexcept;
    [[nodiscard]] static bool IsTrayOrTaskbarWindow(HWND hwnd) noexcept;
    /// Two-pass WebView2 detection (loaded modules + child-process scan).
    /// Hook-thread only — `webView2PositiveCache_` is plain (not atomic).
    [[nodiscard]] bool IsWebView2App(HWND topLevel,
                                     const std::wstring& exeFullPath) noexcept;

    HWINEVENTHOOK focusHook_     = nullptr;  // EVENT_SYSTEM_FOREGROUND
    HWINEVENTHOOK minimizeHook_  = nullptr;  // EVENT_SYSTEM_MINIMIZEEND
    FocusChangedFn onFocusChanged_;
    ReinstallFn    onReinstall_;

    std::atomic<DWORD> lastForegroundPid_{0};
    std::wstring activeExe_;     // any focus (incl. helper windows)
    std::wstring lastRealExe_;   // non-skipAppTracking focus only
    std::wstring previousExe_;   // prev lastRealExe (encoding fallback)

    // HWND is atomic (8B aligned pointer — torn-read-safe on x64); class
    // wstring is NOT atomic. The resulting tuple race is benign: a brief
    // stale-class read causes at worst a 1-keystroke filter miss, which
    // the next focus change recovers.
    std::atomic<HWND> cachedFocusedHwnd_{nullptr};
    std::wstring cachedFocusedClass_;

    std::unordered_map<HWND, AppProfile> appProfileCache_;
    static constexpr size_t kMaxAppProfileCache = 64;

    // Smart-switch
    std::unordered_map<std::wstring, bool> appModeMap_;
    std::atomic<bool> appModeDirty_{false};
    std::atomic<std::uint64_t> lastDirtyTs_{0};
    // RCU-published snapshot of appModeMap_ (worker reads this, never the
    // live map). std::atomic<std::shared_ptr<...>> is the C++20 native
    // form (the free std::atomic_load/store overloads are deprecated in
    // C++20 and MSVC /WX promotes the deprecation to an error).
    std::atomic<std::shared_ptr<const std::unordered_map<std::wstring, bool>>> appModesSnap_;
    SmartSwitchManager smartSwitchMgr_;

    // CJK layout
    bool layoutSuppressed_     = false;
    bool modeBeforeCjk_        = true;
    bool cachedIsCompatLayout_ = true;

    // WebView2 positive-only cache. Hook-thread only (same single-thread
    // invariant as appProfileCache_) — accessed only from inside Classify.
    std::unordered_set<std::wstring> webView2PositiveCache_;

    // Thread-safe dynamic hijacker tracking
    std::unordered_set<std::wstring> dynamicHijackers_;
    mutable std::mutex dynamicHijackersMutex_;

    // Singleton for static WinEventProc dispatch (Win32 callback has no
    // userdata pointer). Mirror of HookLifecycle's per-instance approach
    // — set in Install, cleared in Uninstall.
    static std::atomic<FocusOwner*> s_instance;
};

}  // namespace NextKey
