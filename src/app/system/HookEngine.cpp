// VKey - Keyboard Hook Engine Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "HookEngine.h"
#include "HookHijackDetector.h"  // Anti-Dorion v2 — keyboard-hook hijack detector (safety net)
#include "ReinstallBurstScheduler.h"  // Anti-Dorion v2 — burst reinstall (primary path)
#include "HotkeyManager.h"  // Wave 1 — DispatchHotkeyFromHookThread
#include "Win32CaseMapper.h"
#include "PerfHistogram.h"  // Phase 1 — per-stage histogram (compiles to no-op when VKEY_PERF_HIST undef)
#include "helpers/AppHelpers.h"
#include "output/OutputInjectorFactory.h"  // Sprint 2 T3 — output channel strategy
#include "output/Internal.h"  // Sprint 2 D5 — g_synthCounterCallback bridge
#include "core/engine/CodeTableConverter.h"
#include "core/engine/EngineFactory.h"
#include "core/config/ConfigManager.h"
#include "core/config/ConfigSnapshotBuilder.h"
#include "core/CjkSwitchDecision.h"
#include "core/CommitUndoExemption.h"
#include "core/DigitLedWordDecision.h"
#include "core/MacroCase.h"
#include "core/MacroPrefix.h"
#include "core/ipc/SharedStateManager.h"
#include "core/Debug.h"
#include "core/CrashLog.h"
#include "core/pipeline/BackwardEditFeature.h"
#include "core/pipeline/CommitUndoFeature.h"
#include "core/pipeline/EscRestoreRawFeature.h"
#include "core/pipeline/MacroFeature.h"
#include "core/pipeline/HookCompositionSession.h"
#include "core/pipeline/Intent.h"
#include "core/pipeline/KeyContext.h"
#include "core/pipeline/gates/EnglishBiasGate.h"
#include "core/pipeline/gates/SpellCheckGate.h"
#include "core/pipeline/gates/ToneEscapeGate.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <tlhelp32.h>
#include <vector>

namespace NextKey {

// Wave 3 PR 3.1 (2026-05-23) — WM_APP_REINSTALL_HOOKS / WM_APP_HOOK_COMMAND
// definitions + REINSTALL_REASON_* constants moved into HookLifecycle (the
// owner of the hook thread + LL hooks + mailbox that posts/consumes these
// messages). HookEngine reaches the reinstall path via
// `lifecycle_.PostReinstallHooks(REINSTALL_REASON_*)` — the reason constants
// are exported from HookLifecycle.h.

// ═══════════════════════════════════════════════════════════
// VKEY_ASSERT_HOOK_THREAD — Phase 2d single-writer invariant.
//
// Composition-state mutation entry points (ResetComposition,
// CommitComposition, ClearWordState, ReplaceComposition,
// ReplayCommittedChars, HandleAlphaKey, HandleBackspace,
// ApplyConfigOnHookThread) MUST run on the hook thread. The macro is
// debug-only (NDEBUG elides it) — Release builds pay nothing. Pre-Start
// is a free pass: hookThreadId_ is 0 until HookThreadProc claims it,
// and Start() legitimately runs composition setup on main before the
// hook thread spawns.
// ═══════════════════════════════════════════════════════════
#ifdef NDEBUG
  #define VKEY_ASSERT_HOOK_THREAD() ((void)0)
#else
  #define VKEY_ASSERT_HOOK_THREAD()                                            \
      do {                                                                     \
          const DWORD _expected = lifecycle_.ThreadId();                        \
          if (_expected != 0) {                                                 \
              const DWORD _current = GetCurrentThreadId();                      \
              if (_current != _expected) {                                      \
                  HOOK_LOG(L"VKEY_ASSERT_HOOK_THREAD violated: tid=%lu, expected hook tid=%lu — %hs", \
                           _current, _expected, __func__);                      \
                  assert(_current == _expected &&                               \
                         "composition-state mutation must run on the hook thread"); \
              }                                                                 \
          }                                                                     \
      } while (0)
#endif

// ═══════════════════════════════════════════════════════════
// HOOK_LOG → unified runtime-gated Logger (core/Logger.h).
// Enable from Settings → System → "Bật debug log". Output file is shared
// with NEXTKEY_LOG / TSF_LOG: VKey_<process>_<pid>.log next to
// VKeyApp.exe (falls back to %APPDATA%\VKey\logs\ if install dir is
// read-only). Flushing/closing is owned by the Logger (DLL detach + EXE
// process exit) — hook Start/Stop does NOT toggle the logger lifecycle.
// ═══════════════════════════════════════════════════════════
#define HOOK_LOG(fmt, ...) do {                                              \
    if (::NextKey::Logger::IsEnabled())                                      \
        ::NextKey::Logger::Log(L"[Hook] " fmt, ##__VA_ARGS__);               \
} while (0)

std::atomic<HookEngine*> HookEngine::s_instance{nullptr};

HookEngine::HookEngine() {
    // Wave 3 PR 3.3 — default injector seed moved into OutputDispatcher
    // ctor (dispatcher_'s own NSDMI handles the empty-classification
    // factory call). The hook hot path's `dispatcher_.GetInjector()` is
    // safe to call before any focus event has fired.

    // Wave 2: register the feature pipeline. vietnameseMode_ already has its
    // in-class initializer (true), so the gate's stored reference is live as
    // soon as this ctor body runs. *this is passed as IBackwardEditExecutor;
    // BackwardEditFeature only stores the reference and calls ExecuteReplace
    // later (per-keystroke), never during construction — safe even though the
    // derived HookEngine is still mid-construction here.
    coordinator_.RegisterGate(
        std::make_unique<NextKey::Pipeline::EnglishBiasGate>(vietnameseMode_));  // audit-allow: gate stores const ref, IsRaised() uses .load(acquire)
    // Wave 5: SpellCheckGate raised when engine_->IsEnglishWord() (3-tier
    // English protection bias == HardEnglish). ToneEscapeGate raised when
    // any EscapeKind is active. Both hold ref to the engine_ unique_ptr —
    // safe because the ref stays valid across config reloads; only the
    // pointee swaps. Gate dereferences on every IsRaised call.
    coordinator_.RegisterGate(
        std::make_unique<NextKey::Pipeline::SpellCheckGate>(engine_));
    coordinator_.RegisterGate(
        std::make_unique<NextKey::Pipeline::ToneEscapeGate>(engine_));
    coordinator_.Register(
        std::make_unique<NextKey::Pipeline::BackwardEditFeature>(*this));
    // Wave 3: CommitUndoFeature owns step 2d FSM dispatch. Stage::PreEngine
    // prio 20. *this is the ICommitUndoExecutor backing — feature delegates
    // synchronously to HandleCommitUndo(vk) which adapts to HandleCommitUndoFsm.
    coordinator_.Register(
        std::make_unique<NextKey::Pipeline::CommitUndoFeature>(*this));
    // Wave 4a: EscRestoreRawFeature handles hotkey-triggered ESC (or any
    // CancelComposition trigger) at PreEngine prio 40 — fires AFTER
    // CommitUndoFeature so the FSM's ESC-exemption check runs first.
    coordinator_.Register(
        std::make_unique<NextKey::Pipeline::EscRestoreRawFeature>(*this));
    // Wave 4b: MacroFeature owns macro tracking + expansion at PreEngine
    // prio 30 (between CommitUndo 20 and EscRestoreRaw 40). Adapter contains
    // EN-mode + VN-mode macro logic transcribed from HandlePreDispatch.
    coordinator_.Register(
        std::make_unique<NextKey::Pipeline::MacroFeature>(*this));
}

HookEngine::~HookEngine() {
    Stop();
}

void HookEngine::CommitPending() {
    std::lock_guard<std::mutex> _lock(stateMutex_);
    if (engine_ && engine_->Count() > 0) {
        CommitComposition();
    }
}

// Wave 2 (2026-05-23): lock-free. Formerly required caller-held stateMutex_
// (Sprint 1 D11 contract) because it wrote plain-bool cache fields. Those
// fields are gone; remaining work is atomic stores + RCU-published settings
// (injector_, Logger). Safe to call from any thread.
void HookEngine::ApplyConfig(const TypingConfig& config) {
    autoCaps_.store(config.autoCaps, std::memory_order_release);
    macroEnabled_.store(config.macroEnabled, std::memory_order_release);
    macroInEnglish_.store(config.macroInEnglish, std::memory_order_release);
    autoCapsMacro_.store(config.autoCapsMacro, std::memory_order_release);
    // Push the suggestKeepChars flag to the live injector so ShouldEmitBait
    // sees the latest user choice without needing a focus change to swap
    // injectors. Focus-change paths re-apply this from the config snapshot.
    if (auto inj = dispatcher_.GetInjector(); inj) {
        inj->SetSuggestKeepChars(config.suggestKeepChars);
    }
    // Runtime file-logger gate (Settings → System → "Bật debug log").
    ::NextKey::Logger::SetEnabled(config.debugLogEnabled);
}

void HookEngine::ApplyHotkeyRegistry(HotkeyRegistry registry) {
    // RCU publish — readers (hook hot path) pick up on next load(). Old
    // registry stays alive until any in-flight Matches() returns.
    hotkeys_.store(std::make_shared<const HotkeyRegistry>(std::move(registry)),
                   std::memory_order_release);
}

namespace {

// Map L/R modifier VK variants down to their canonical form so HotkeyRegistry
// triggers (stored as canonical VK_CONTROL/VK_MENU/VK_SHIFT/VK_LWIN) match
// hook events (which report VK_LCONTROL/VK_RCONTROL etc.).
[[nodiscard]] uint32_t CanonicalModifierVk(DWORD vk) noexcept {
    switch (vk) {
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU:    case VK_RMENU:    return VK_MENU;
    case VK_LSHIFT:   case VK_RSHIFT:   return VK_SHIFT;
    case VK_RWIN:                       return VK_LWIN;  // collapse to one Win
    default:                            return vk;
    }
}

// Pack the cached modifier booleans into a HotkeyRegistry-style bitmask.
[[nodiscard]] uint32_t ComputeModMask(bool ctrl, bool shift, bool alt, bool win) noexcept {
    uint32_t mask = 0;
    if (ctrl)  mask |= kModCtrl;
    if (shift) mask |= kModShift;
    if (alt)   mask |= kModAlt;
    if (win)   mask |= kModWin;
    return mask;
}

// Canonical-VK → modTapCount_[] / modTapLastTs_[] index. Returns -1 for any
// VK that isn't one of the 4 modifiers we track. Keeps the slots stable so
// the array can be a flat fixed-size buffer.
[[nodiscard]] int ModIdxFor(uint32_t canonicalVk) noexcept {
    switch (canonicalVk) {
    case VK_CONTROL: return 0;
    case VK_SHIFT:   return 1;
    case VK_MENU:    return 2;
    case VK_LWIN:    return 3;
    default:         return -1;
    }
}

}  // namespace

bool HookEngine::Start(HINSTANCE hInstance, const TypingConfig& config,
                        bool initialVietnamese) {
    if (lifecycle_.IsRunning()) return false;  // Already running

    // Enable the file logger before the first HOOK_LOG so the start banner is
    // captured when the user already had the toggle on. ApplyConfig() re-asserts
    // this below for subsequent config reloads.
    ::NextKey::Logger::SetEnabled(config.debugLogEnabled);
    HOOK_LOG(L"=== HookEngine::Start ===");

    s_instance = this;
    // Wave 3 PR 3.3 — OutputDispatcher owns the synth-counter wiring.
    // Install sets dispatcher's s_instance + g_synthCounterCallback in
    // one step. Idempotent. Wired AFTER HookEngine::s_instance so a hook
    // thread already racing wouldn't see a half-initialized dispatcher
    // (we're still on main here; hook thread starts further down).
    dispatcher_.Install();
    currentMethod_.store(config.inputMethod, std::memory_order_release);
    config_.store(std::make_shared<const TypingConfig>(config), std::memory_order_release);
    // Wave 2 (2026-05-23): ApplyConfig is now lock-free (all writes are atomic
    // or RCU-publish). Sprint 1 D11's "caller holds stateMutex_" contract
    // dropped. Single-threaded init here; ApplyConfig safe to call directly.
    ApplyConfig(config);
    // Load unified hotkey registry. On first launch after v3 upgrade, the
    // `[[hotkeys]]` section is missing — migrate reads legacy `[features]`
    // toggles directly from TOML and persists `[hotkey_state]` so future
    // launches read the new schema directly.
    ApplyHotkeyRegistry(ConfigManager::MigrateLegacyHotkeysIfNeeded(
        ConfigManager::GetConfigPath()));
    autoCapState_ = AutoCapState::Idle;
    engine_ = EngineFactory::Create(config);
    vietnameseMode_.store(initialVietnamese, std::memory_order_release);

    // Create shared memory for smart switch and load persisted English-mode apps.
    // Bug C fix (2026-05-26): persistence used to be gated on startupMode_==2
    // (Remember) — but smart_switch is conceptually independent of the initial
    // mode choice. With smart_switch on, the user expects per-app state to
    // survive restarts regardless of whether startup begins in V/E/Remember.
    if (config.smartSwitch) {
        (void)focus_.Smart().Create();
        // V2 schema (with legacy [smart_switch].english_mode_apps fallback
        // for one-time silent migration). See
        // docs/plans/2026-05-28-smart-switch-persistence-design.md §2.
        auto persisted = ConfigManager::LoadSmartSwitchApps(ConfigManager::GetConfigPath());
        focus_.AppModeMap() = std::move(persisted);
        if (!focus_.AppModeMap().empty()) {
            focus_.Smart().LoadFromMap(focus_.AppModeMap());
        }
        // Publish initial snapshot so the worker has something to flush
        // if dirty fires before the first map mutation.
        focus_.PublishAppModesSnapshot();
    }

    currentCodeTable_.store(config.codeTable, std::memory_order_release);
    globalCodeTable_.store(config.codeTable, std::memory_order_release);
    globalInputMethod_.store(config.inputMethod, std::memory_order_release);

    // Cache initial SharedState values (pointer set by main.cpp via SetSharedStateReader)
    if (sharedStatePtr_) {
        SharedState state = sharedStatePtr_->Read();
        if (state.IsValid()) {
            lastFeatureFlags_.store(state.GetFeatureFlags(), std::memory_order_release);
            lastSpellCheck_.store(state.spellCheck, std::memory_order_release);
            lastInputMethod_.store(state.inputMethod, std::memory_order_release);
            lastCodeTable_.store(state.codeTable, std::memory_order_release);
            lastConfigGeneration_.store(state.configGeneration, std::memory_order_release);
            // Wave 3 PR 3.8 — seed the toggle-hotkey cache so the first
            // QuickSync slow body doesn't fire a spurious callback. The
            // initial HotkeyManager binding came from TOML via WireHotkeys
            // at startup; SharedState's hotkey field matches that on a
            // clean run (Settings dialog writes both paths in sync).
            lastToggleHotkey_ = state.GetHotkey();
        }
    }

    // Phase 3d — single rebuild: TOML parse for overrides/excluded/TSF/
    // macros + atomic snapshot publish. Replaces the four legacy
    // Reload* + PublishConfigSnapshot calls from earlier.
    RebuildSnapshotFromToml(
        static_cast<std::uint32_t>(lastConfigGeneration_.load(std::memory_order_acquire)));

    // Wave 3 PR 3.1: HookLifecycle owns the dedicated hook thread + LL hooks
    // + mailbox. We pass our LL callbacks (still HookEngine statics via
    // s_instance) and a drain callback that fans out to DrainHookCommands.
    if (!lifecycle_.Start(hInstance, LowLevelKeyboardProc, LowLevelMouseProc,
                          [this] { DrainHookCommands(); })) {
        HOOK_LOG(L"FAILED to install keyboard hook (lifecycle Start returned false)");
        return false;
    }

    // Wave 3 PR 3.2: WinEvent hook lifecycle moved into FocusOwner. Reinstall
    // gate stays here (we know lifecycle's runtime state); FocusOwner stays
    // decoupled from HookLifecycle.
    if (!focus_.Install(
            [this](HWND hwnd) { OnFocusChanged(hwnd); },
            [this](WPARAM reason) {
                if (lifecycle_.ThreadId()) {
                    lifecycle_.PostReinstallHooks(reason);
                }
            })) {
        HOOK_LOG(L"FAILED to install WinEvent hook");
        // Don't return false — focus events are nice-to-have; LL hook still works.
    }

    // Sprint 1 D10: 200 ms focus / CJK poll is no longer driven by SetTimer.
    // The owning EXE wires MainThreadWorker::SetTickHandler([](){ OnTickPoll(); })
    // and SetTickInterval(200ms); Start does not own the cadence anymore.

    // Phase 1 perf histogram (docs/plans/2026-05-19-architecture-review-design.md).
    // Path: %APPDATA%\VKey\perf-histogram-<pid>-<startTs>.log. The Enabled() gate
    // is sourced from SharedState.diagFlags inside QuickSyncFromSharedState; we
    // seed it here from the TOML toggle so the very first keystroke is captured.
    {
        wchar_t pathBuf[MAX_PATH];
        const DWORD startTs = GetTickCount();
        const DWORD pid = GetCurrentProcessId();
        const std::wstring base = ConfigManager::GetAppDataDirectory();
        const int n = swprintf_s(pathBuf, MAX_PATH,
            L"%ls\\perf-histogram-%lu-%lu.log", base.c_str(), pid, startTs);
        if (n > 0) {
            Perf::Histogram::SetLogPath(pathBuf);
        }
        Perf::Histogram::SetEnabled(config.perfHistogramEnabled);
    }

    // Anti-Dorion v2 — construct the hijack detector. No thread of its own
    // (Pillar 2 "Nhẹ"): the detector is a stateless processor; OnTickPoll
    // calls Poll() while a Chromium-class app is foreground. Owner retunes
    // MainThreadWorker cadence to ~40ms during chromium-fg sessions for
    // adequate sampling; falls back to adaptive cadence when not.
    {
        HookHijackDetector::Callbacks cb;
        cb.readHookFireCount = [this]() noexcept -> uint64_t {
            return hookFireCount_.load(std::memory_order_acquire);
        };
        cb.requestReinstall = [this]() noexcept {
            HOOK_LOG(L"  HijackDet: BYPASS confirmed → reinstall (reason=hijack)");
            // REINSTALL_REASON_HIJACK gets a tighter throttle (100ms vs
            // 500ms for focus/mouse paths) since the detector has its own
            // internal cooldown and only fires on confirmed bypass.
            lifecycle_.PostReinstallHooks(REINSTALL_REASON_HIJACK);

            // Dynamically register the active application as a hijacker so we
            // proactively reinstall the hook next time it is focused.
            const std::wstring active = focus_.ActiveExe();
            if (!active.empty()) {
                focus_.RegisterDynamicHijacker(active);
            }
        };
        cb.injectGhostChar = [this](wchar_t ch) noexcept {
            HOOK_LOG(L"  HijackDet: ghost-inject ch='%c'",
                     (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?');
            // Route via HookLifecycle's WM_APP_GHOSTKEY → HandleGhostChar
            // on the hook thread (single-writer §12). Detector's caller
            // thread never mutates engine state directly.
            lifecycle_.PostGhostKey(ch);
        };
        cb.readKeyboardState = [](uint8_t (&state)[256]) noexcept -> bool {
            // CRITICAL: GetKeyboardState is thread-local — only updates as the
            // calling thread removes keyboard messages from its queue. Polling
            // from MainThreadWorker (which doesn't process keyboard messages)
            // would always see a stale-zero state → detector never sees drift.
            // GetAsyncKeyState reads the kernel-global key state, thread-
            // agnostic; 256 syscalls/poll is acceptable cold-path cost
            // (~40ms cadence × 256 = light), and each call is nanosecond-scale.
            for (int vk = 0; vk < 256; ++vk) {
                const SHORT s = ::GetAsyncKeyState(vk);
                // High bit (0x8000) = currently down. Low bit (just-pressed
                // sentinel) is reset by the read; we don't need it.
                state[vk] = (s & 0x8000) ? 0x80 : 0;
            }
            return true;
        };
        cb.translateVkToChar = [](uint8_t vk, const uint8_t (&stateNow)[256]) noexcept -> wchar_t {
            // Use foreground thread's keyboard layout — most accurate for
            // what the user is typing into. Falls back to current thread
            // layout if the foreground HWND can't be resolved.
            HKL hkl = nullptr;
            if (HWND fg = ::GetForegroundWindow(); fg) {
                const DWORD tid = ::GetWindowThreadProcessId(fg, nullptr);
                if (tid != 0) hkl = ::GetKeyboardLayout(tid);
            }
            if (!hkl) hkl = ::GetKeyboardLayout(0);
            const UINT scan = ::MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, hkl);
            wchar_t out[8] = {};
            const int n = ::ToUnicodeEx(vk, scan, stateNow, out,
                                        static_cast<int>(sizeof(out) / sizeof(out[0])),
                                        0, hkl);
            // n>0: chars written. n==0: no translation. n<0: dead key.
            return (n > 0) ? out[0] : wchar_t{0};
        };
        hijackDetector_ = std::make_unique<HookHijackDetector>(std::move(cb));
        lifecycle_.SetGhostKeyHandler([this](wchar_t ch) noexcept {
            HandleGhostChar(ch);
        });
    }

    // Adaptive-tick (#2): seed the activity clock to LAUNCH time. lastActivityTickMs_
    // is otherwise 0, so the FIRST worker tick computes idleMs = system uptime
    // ≥ kIdleStopThreshMs and STOPS the worker ~200ms after launch — killing the
    // CJK/Win+Space poll, the foreground-PID fallback, the config-reload drain
    // and smart-switch persistence until the first keystroke re-arms it. Seeding
    // here makes "idle" measure from launch, not boot. See AdaptiveTick.h.
    lastActivityTickMs_.store(GetTickCount64(), std::memory_order_relaxed);

    // Anti-Dorion v2 PRIMARY path — staggered reinstall burst. On focus-to-
    // chromium-class app, schedule 3 reinstalls at 300/800/1500 ms; at least
    // one lands AFTER Dorion's own LL hook install → VKey ends up at the
    // chain head. Scheduler uses Win32 thread-pool timers (no VKey-owned
    // thread per Pillar 2); generation-counter cancellation handles the
    // focus-out race. See ReinstallBurstScheduler.{h,cpp} for the contract
    // and the 6 unit tests in tests/ReinstallBurstSchedulerTest.cpp.
    {
        // A4: dedicated timer queue so Stop() can block-drain in-flight burst
        // callbacks before the scheduler dies. CreateTimerQueue can fail (NULL);
        // we do NOT fall back to the default process queue, because Stop() can't
        // drain that one (the default queue isn't deletable) and the generation_
        // UAF would silently return. On failure we leave reinstallBurstScheduler_
        // null (burst disabled) and rely on the drift-gated detector as the
        // reactive net — see the guard around make_unique below.
        burstTimerQueue_ = CreateTimerQueue();
        ReinstallBurstScheduler::Callbacks bcb;
        bcb.schedule = [this](uint32_t delayMs, std::function<void()> fire) noexcept {
            // Heap context survives the synchronous return; the thread-pool
            // callback fires it after `delayMs`, then self-deletes the timer
            // handle and the context. unique_ptr handoff pattern (Rule 3):
            // own the context while wiring up the timer, .release() on success
            // hands ownership to Win32, the callback reclaims via unique_ptr
            // to RAII-cleanup on scope exit. Failure path's unique_ptr
            // destructor frees the context inline — no leak.
            struct TimerCtx {
                std::function<void()> fn;
                HANDLE handle{nullptr};
                HANDLE queue{nullptr};
            };
            auto ctx = std::make_unique<TimerCtx>(
                TimerCtx{std::move(fire), nullptr, burstTimerQueue_});
            const auto poolCallback = [](PVOID p, BOOLEAN /*timedOut*/) {
                // RAII reclaim — owned destructor frees the context whether
                // fn() throws or returns normally.
                std::unique_ptr<TimerCtx> owned(static_cast<TimerCtx*>(p));
                try { owned->fn(); }
                catch (const std::exception& e) {
                    CrashLog(L"ReinstallBurstScheduler.fire", e.what());
                }
                catch (...) {
                    CrashLog(L"ReinstallBurstScheduler.fire", "(non-std exception)");
                }
                // Self-delete the timer queue entry from OUR dedicated queue
                // (A4). NULL completion-event arg = don't wait (we ARE the
                // callback) — safe non-blocking cleanup, and never races Stop()'s
                // DeleteTimerQueueEx (which waits for callbacks to finish).
                if (owned->handle) {
                    DeleteTimerQueueTimer(owned->queue, owned->handle, nullptr);
                }
            };
            if (CreateTimerQueueTimer(&ctx->handle, burstTimerQueue_, poolCallback, ctx.get(),
                                       static_cast<DWORD>(delayMs), 0,
                                       WT_EXECUTEDEFAULT | WT_EXECUTEONLYONCE)) {
                // Ownership transferred to Win32 (callback reclaims).
                (void)ctx.release();
            } else {
                HOOK_LOG(L"  CreateTimerQueueTimer FAILED err=%lu — burst step lost",
                         GetLastError());
                // ctx unique_ptr destructor frees the context — no leak.
            }
        };
        bcb.postReinstall = [this](uint32_t reason) noexcept {
            HOOK_LOG(L"  BurstReinstall: posting reinstall reason=%u", reason);
            lifecycle_.PostReinstallHooks(static_cast<WPARAM>(reason));
        };
        // Only wire the scheduler if we own a dedicated, drainable queue (A4).
        if (burstTimerQueue_) {
            reinstallBurstScheduler_ =
                std::make_unique<ReinstallBurstScheduler>(std::move(bcb));
        } else {
            HOOK_LOG(L"  CreateTimerQueue FAILED err=%lu — burst reinstall disabled "
                     L"(detector still covers hijacks)", GetLastError());
        }
    }

    NEXTKEY_LOG(L"HookEngine started (method=%d, vietnamese=%d)",
                static_cast<int>(currentMethod_.load(std::memory_order_acquire)),
                vietnameseMode_.load(std::memory_order_acquire));
    HOOK_LOG(L"Hook installed OK (method=%d, vietnamese=%d)",
             static_cast<int>(currentMethod_.load(std::memory_order_acquire)),
             vietnameseMode_.load(std::memory_order_acquire));
    return true;
}

void HookEngine::Stop() {
    HOOK_LOG(L"=== HookEngine::Stop ===");
    // Anti-Dorion v2 — detector has no thread of its own (Pillar 2). Close
    // the gate so any in-flight Poll from MainThreadWorker no-ops cleanly,
    // then drop the pointer at scope exit. No explicit Stop() / join needed.
    if (hijackDetector_) hijackDetector_->SetChromiumClassActive(false);
    // Cancel any pending burst-reinstall callbacks — in-flight thread-pool
    // timers would otherwise post WM_APP_REINSTALL_HOOKS to a hook thread
    // that's about to exit. The generation bump makes those callbacks
    // no-op when they fire; the timer queue cleans itself up.
    if (reinstallBurstScheduler_) reinstallBurstScheduler_->Cancel();
    // A4: block-drain in-flight burst timers BEFORE reinstallBurstScheduler_ is
    // destroyed (it lives until the HookEngine dtor). DeleteTimerQueueEx with
    // INVALID_HANDLE_VALUE waits for any executing callback to finish and cancels
    // unfired ones — so no thread-pool callback can read the scheduler's
    // generation_ through a dangling object at process exit. Cancel() above
    // already neutralizes the POST; this closes the UAF on the load itself.
    if (burstTimerQueue_) {
        // Timers that DeleteTimerQueueEx cancels BEFORE they fire leak their heap
        // TimerCtx (the callback that would free it never runs) — bounded to ≤3
        // tiny contexts, reclaimed by the OS at process exit (Stop() is
        // shutdown-only here). Not worth handle-tracking machinery; the prior
        // behaviour was a UAF, so this is a strict improvement.
        DeleteTimerQueueEx(burstTimerQueue_, INVALID_HANDLE_VALUE);
        burstTimerQueue_ = nullptr;
    }
    // Phase 1 perf histogram: final flush before we tear down so the
    // last 60s window of samples reaches disk. Idempotent.
    Perf::Histogram::Stop();
    // Wave 3 PR 3.1: HookLifecycle handles thread shutdown + LL hook teardown
    // (unhook MUST happen on the installer thread per MSDN — lifecycle owns
    // that thread). Wave 3 PR 3.2: WinEvent hooks moved to FocusOwner.
    // Wave 3 PR 3.3: dispatcher owns synth-counter wiring.
    // Order: join hook thread → tear down dispatch (no in-flight SendInput
    // can race once the thread is gone) → tear down focus state. Mirrors
    // reverse-declaration destruction order (lifecycle → dispatcher → focus).
    lifecycle_.Stop();
    dispatcher_.Uninstall();
    focus_.Uninstall();

    // Smart-switch force-flush — MUST run after lifecycle_.Stop() so the
    // hook thread is joined and reading focus_.AppModeMap() directly is
    // safe (no concurrent mutation possible). Bug 3 fix (2026-05-28):
    // pre-fix code ran the legacy save BEFORE lifecycle_.Stop() →
    // cross-thread live-map read race. See design §1 + §3.
    FlushSmartSwitchOnStop();

    // Sprint 1 D10: focusPollTimer_ retired — owner stops its
    // MainThreadWorker (which owns the 200 ms tick) before us.
    if (s_instance == this) {
        s_instance = nullptr;
    }
    focus_.ResetLayoutState();
    NEXTKEY_LOG(L"HookEngine stopped");
}

// Wave 3 PR 3.1 (2026-05-23) — HookThreadProc body moved to
// HookLifecycle::ThreadProc. Same pump structure, same WM_APP_* dispatch,
// same throttled reinstall. HookLifecycle invokes our DrainHookCommands via
// a callback registered at lifecycle_.Start().

void HookEngine::ToggleVietnameseMode() {
    // Phase 2c: ToggleVietnameseMode is called from any thread (tray menu
    // on main, hotkey on either main or the hook pump itself when fired
    // via HotkeyRegistry, modifier-only double-tap). All composition-state
    // writes (CommitComposition, vietnameseMode_, appModeMap_, etc.) must
    // happen on the hook thread (Rule 11.3 single-writer). Post the bit
    // and let the drain do the work.
    lifecycle_.Mailbox().Post(HookCommand::kToggleVN);
}

void HookEngine::SetCodeTable(CodeTable ct) {
    std::lock_guard<std::mutex> _lock(stateMutex_);
    // Commit any pending composition before switching
    if (ct != currentCodeTable_.load(std::memory_order_acquire) && engine_->Count() > 0) {
        CommitComposition();
    }

    currentCodeTable_.store(ct, std::memory_order_release);

}

CodeTable HookEngine::GetCodeTable() const noexcept {
    // Priority 1: Manual per-app override (set explicitly by user)
    // Check previousExe_ first as a fallback: on the first focus event after startup,
    // activeExe_ may not yet reflect the typing app.
    // Phase 3c: read from RCU snapshot — lock-free, safe on any thread.
    auto snap = configSnapshot_.load(std::memory_order_acquire);
    auto lookupOverride = [&](const std::wstring& exe) -> const CodeTable* {
        if (exe.empty() || !snap) return nullptr;
        auto it = snap->appEncodingOverrides.find(exe);
        return (it != snap->appEncodingOverrides.end()) ? &it->second : nullptr;
    };
    if (auto* v = lookupOverride(focus_.PreviousExe())) return *v;
    if (auto* v = lookupOverride(focus_.ActiveExe()))   return *v;

    return currentCodeTable_.load(std::memory_order_acquire);
}

void HookEngine::QuickSyncFromSharedState() {
    // Pre-T3 Minor 2 fix (Rule #11.3): hot path is lock-free. The common
    // case — no SharedState change since the last call — returns before
    // any atomic ops, eliminating the per-keystroke contention with
    // main-thread writers (ToggleVietnameseMode, SetCodeTable, …) that
    // showed up as p99 jitter under chaos.
    //
    // Wave 2 (2026-05-23): slow path is now ALSO lock-free. Pre-Wave-2 the
    // slow-path body ran under stateMutex_ to serialise concurrent QuickSync
    // callers. Post-Wave-2 the body uses a CAS on lastEpoch_ to claim
    // exclusive processing of each SharedState epoch transition — at most
    // one caller's full apply-and-publish runs per epoch claim. Concurrent
    // callers whose CAS fails return early; newer epochs they observed are
    // re-processed on the next QuickSync call (bounded recovery ≤200 ms via
    // worker tick or sooner via keystroke). The mutex is preserved on
    // CommitPending / SetCodeTable for engine-state mutation; QuickSync no
    // longer touches it.
    if (!sharedStatePtr_) return;

    // Fast path: lock-free atomic epoch check. SharedState::ReadEpoch is
    // a memory-mapped 32-bit seqlock counter; lastEpoch_ is std::atomic.
    // Common case under steady-state typing: epoch unchanged → return
    // without any stateMutex_ acquire. Cost: ~5 ns total.
    uint32_t epoch = sharedStatePtr_->ReadEpoch();
    uint32_t seenEpoch = lastEpoch_.load(std::memory_order_acquire);
    if (epoch == seenEpoch && (epoch & 1) == 0) return;

    // Wave 3 PR 3.6 — Rule 11.2 + doctrine §12.4 (worker-thread doctrine):
    // hook thread MUST NOT execute the slow body. Two heap-allocating ops
    // live below — `std::make_shared<TypingConfig>` (line ~518) and the
    // `ReloadFromToml` branch behind the configGeneration check — and
    // running either from `LowLevelKeyboardProc` violates Rule 11.2's
    // "no malloc on hook" ceiling. Signal the worker thread so it re-runs
    // QuickSync on its own thread; intentionally do NOT advance lastEpoch_
    // so the worker still observes the change.
    //
    // Coalescing property (§12.4): a burst of SharedState changes between
    // worker wakes collapses into one drain. The slow body sees the latest
    // state on its single execution.
    if (const DWORD _hookTid = lifecycle_.ThreadId();
        _hookTid != 0 && GetCurrentThreadId() == _hookTid) {
        if (workerSignalFn_) workerSignalFn_();
        return;
    }

    // Slow path. Wave 2 (2026-05-23) — stateMutex_ DROPPED. The full body
    // (last* updates, config_ publish, ApplyConfig) was serialised by the
    // mutex pre-Wave-2; now it's serialised by a CAS on lastEpoch_ that
    // atomically claims each epoch transition. At most one caller's CAS
    // succeeds per (seenEpoch → state.epoch) edge; losers return without
    // publishing. This eliminates the lost-update race possible if both
    // callers raced their unconditional .store + RCU publishes (older
    // store could land last, overwriting newer published config).
    //
    // Trade-off: a CAS loser that observed a NEWER state.epoch than the
    // winner is dropped — but recovery is bounded ≤200 ms because the
    // next QuickSync caller (worker tick, hook keystroke, main public-API)
    // observes lastEpoch < SharedState.epoch and claims the missed epoch.
    epoch = sharedStatePtr_->ReadEpoch();
    seenEpoch = lastEpoch_.load(std::memory_order_acquire);
    if (epoch == seenEpoch && (epoch & 1) == 0) return;

    SharedState state = sharedStatePtr_->Read();
    if (!state.IsValid()) return;

    // CAS claim — exclusive entry to the slow-path body for this epoch
    // transition. If `seenEpoch` is stale (another caller already claimed),
    // the CAS fails and we return. The expected-value contract on
    // compare_exchange_strong overwrites `seenEpoch` with the observed
    // value on failure; we don't use it after, so the side-effect is
    // harmless.
    if (!lastEpoch_.compare_exchange_strong(seenEpoch, state.epoch,
            std::memory_order_release, std::memory_order_acquire)) {
        return;
    }

    // Phase 1: surface SharedState.diagFlags bit 0 into the perf histogram
    // gate. SetEnabled is lock-free.
    Perf::Histogram::SetEnabled((state.diagFlags & DiagFlags::PERF_HISTOGRAM) != 0);

    // ── Config generation check: detect TOML changes from Settings/subdialogs ──
    // When configGeneration changes, do a full TOML reload (macros, excluded apps, etc.).
    // Replaces the old ConfigEvent (Named Event + WaitForSingleObject syscall).
    //
    // Phase 3c thread-aware routing:
    //   • Hook thread → defer to worker (Rule 11.2 — TOML parse is forbidden
    //     here, ~1-10 ms). Set `pendingConfigReload_`; the next OnTickPoll
    //     drains it and runs ReloadFromToml on the worker thread.
    //   • Worker / main → run inline. Already on a thread where TOML parse
    //     is acceptable, no point bouncing through another tick.
    if (state.configGeneration != lastConfigGeneration_.load(std::memory_order_acquire)) {
        if (const DWORD _hookTid = lifecycle_.ThreadId(); _hookTid != 0 && GetCurrentThreadId() == _hookTid) {
            pendingConfigReload_.store(true, std::memory_order_release);
            NEXTKEY_LOG(L"HookEngine: configGeneration bump (%u) seen on hook — deferring Reload to worker tick",
                        state.configGeneration);
        } else {
            lastConfigGeneration_.store(state.configGeneration, std::memory_order_release);
            NEXTKEY_LOG(L"HookEngine: configGeneration changed (%u), full TOML reload", state.configGeneration);
            ReloadFromToml();
        }
    }

    // Wave 3 PR 3.8 — toggle-hotkey live propagation from SharedState.
    //
    // SettingsDialog::syncToSharedState writes the new hotkey into
    // SharedState immediately (state.SetHotkey) but defers the TOML save
    // by 30 s. Pre-3.8 the only reload path was `ReloadFromToml()` fired
    // from the configGeneration check above, which read STALE TOML data
    // and `HotkeyManager::UpdateHotkey` got the old binding until the
    // user closed the Settings dialog (WM_CLOSE forces flush).
    //
    // Doctrine: SharedState is the live config bus, TOML is the
    // persistence layer. The hotkey field lives on both — read from
    // SharedState here so HotkeyManager sees the fresh binding within
    // one QuickSync cycle (~ms latency vs 30 s).
    //
    // Must run BEFORE the ff/sc/im/ct early-return below — those four
    // are engine-state flags; the hotkey doesn't depend on any of them,
    // so a Settings change that only touches the hotkey would short-
    // circuit through the early-return without our diff running.
    {
        const HotkeyConfig newHk = state.GetHotkey();
        if (newHk != lastToggleHotkey_) {
            NEXTKEY_LOG(L"HookEngine: toggle hotkey changed (mods=C%dS%dA%dW%d vk=0x%02X → C%dS%dA%dW%d vk=0x%02X)",
                        lastToggleHotkey_.ctrl, lastToggleHotkey_.shift,
                        lastToggleHotkey_.alt, lastToggleHotkey_.win,
                        lastToggleHotkey_.vk,
                        newHk.ctrl, newHk.shift, newHk.alt, newHk.win, newHk.vk);
            lastToggleHotkey_ = newHk;
            if (hotkeyChangedCallback_) {
                hotkeyChangedCallback_(newHk);
            }
        }
    }

    uint32_t ff = state.GetFeatureFlags();
    uint8_t sc = state.spellCheck;
    uint8_t im = state.inputMethod;
    uint8_t ct = state.codeTable;

    // No change → no-op (cheap: integer compares on mapped memory)
    if (ff == lastFeatureFlags_.load(std::memory_order_acquire) &&
        sc == lastSpellCheck_.load(std::memory_order_acquire) &&
        im == lastInputMethod_.load(std::memory_order_acquire) &&
        ct == lastCodeTable_.load(std::memory_order_acquire)) return;
    lastFeatureFlags_.store(ff, std::memory_order_release);
    lastSpellCheck_.store(sc, std::memory_order_release);
    lastInputMethod_.store(im, std::memory_order_release);
    lastCodeTable_.store(ct, std::memory_order_release);

    NEXTKEY_LOG(L"HookEngine: SharedState changed (ff=0x%04X, spell=%d, method=%d, ct=%d)", ff, sc, im, ct);

    TypingConfig cfg = *config_.load(std::memory_order_acquire);
    DecodeFeatureFlags(ff, cfg);
    cfg.spellCheckEnabled = sc != 0;
    cfg.inputMethod = static_cast<InputMethod>(im);
    cfg.codeTable = static_cast<CodeTable>(ct);

    bool methodChanged = (currentMethod_.load(std::memory_order_acquire) != cfg.inputMethod);
    bool codeTableChanged = (currentCodeTable_.load(std::memory_order_acquire) != cfg.codeTable);
    ApplyConfig(cfg);
    config_.store(std::make_shared<const TypingConfig>(cfg), std::memory_order_release);

    // P3e fix: defer engine recreate to ApplyConfigOnHookThread. QuickSync's
    // slow path runs on whichever thread called it (worker via OnTickPoll →
    // OnFocusChanged, or main via SyncConfigFromSharedState). The engine
    // swap + CommitComposition must run on the hook thread to avoid the
    // race that surfaced under `-InjectConfigReloadMs 50` chaos.
    if (methodChanged) {
        lifecycle_.Mailbox().Post(HookCommand::kConfigApply);
    }

    if (codeTableChanged) {
        currentCodeTable_.store(cfg.codeTable, std::memory_order_release);
        globalCodeTable_.store(cfg.codeTable, std::memory_order_release);
    }

    {
        // Phase 3d: macroEnabled toggled but configGeneration didn't bump
        // (typical case — user flips the macro feature switch). Compare
        // the snapshot's macro presence against the new desired state;
        // if they disagree, rebuild + republish. On the hook thread this
        // path is now deferred via pendingConfigReload_ (same as the
        // configGeneration-bump path) so TOML parse stays off-hook.
        const bool macroOn = macroEnabled_.load(std::memory_order_acquire);
        auto snap = configSnapshot_.load(std::memory_order_acquire);
        const bool snapHasMacros = snap && !snap->macroTable.empty();
        if (macroOn != snapHasMacros) {
            if (const DWORD _hookTid = lifecycle_.ThreadId(); _hookTid != 0 && GetCurrentThreadId() == _hookTid) {
                pendingConfigReload_.store(true, std::memory_order_release);
            } else {
                RebuildSnapshotFromToml(
                    static_cast<std::uint32_t>(lastConfigGeneration_.load(std::memory_order_acquire)));
            }
        }
    }
}

void HookEngine::SyncConfigFromSharedState() {
    QuickSyncFromSharedState();
}

void HookEngine::ReloadFromToml() {
    PERF_SCOPE(::NextKey::Perf::Stage::ConfigReload);
    NEXTKEY_LOG(L"HookEngine: full TOML reload");

    // Read TOML for fields not in SharedState (beep, smartSwitch, excludeApps, hotkey)
    auto config = ConfigManager::LoadOrDefault();

    // Override with SharedState for fields that Settings updates immediately
    // (TOML may be stale due to deferred save)
    if (sharedStatePtr_) {
        SharedState state = sharedStatePtr_->Read();
        if (state.IsValid()) {
            config.inputMethod = static_cast<InputMethod>(state.inputMethod);
            config.spellCheckEnabled = state.spellCheck != 0;
            DecodeFeatureFlags(state.GetFeatureFlags(), config);
            NEXTKEY_LOG(L"HookEngine: read SharedState (epoch=%u, featureFlags=0x%04X)",
                        state.epoch, state.GetFeatureFlags());
        }
    }

    // Smart-switch off→on transition: load persisted apps from TOML on this
    // worker thread (Rule §11.2 — TOML parse is forbidden on hook), then
    // stash for ApplyConfigOnHookThread to swap into the live appModeMap_.
    // Without this, mid-session enabling of smart_switch would leave the
    // map empty and the first focus-driven mutation would overwrite the
    // user's existing TOML entries with an empty `[smart_switch.apps]`.
    {
        const auto prevCfg = config_.load(std::memory_order_acquire);
        const bool wasOn = prevCfg && prevCfg->smartSwitch;
        if (!wasOn && config.smartSwitch) {
            auto loaded = std::make_shared<const std::unordered_map<std::wstring, bool>>(
                ConfigManager::LoadSmartSwitchApps(ConfigManager::GetConfigPath()));
            pendingAppModeMap_.store(std::move(loaded), std::memory_order_release);
            NEXTKEY_LOG(L"SmartSwitch: smart_switch off→on detected, deferred load via ApplyConfig");
        }
    }

    // P3e fix — single-writer for `engine_`. Pre-P3e, this function called
    // CommitComposition + `engine_ = EngineFactory::Create(...)` inline.
    // Post-P3c, ReloadFromToml runs on the worker thread (Rule 11.2 forbids
    // TOML parse on hook), so the inline engine swap raced against the hook
    // hot path's `engine_->Peek/Push/Count` reads — UAF discovered by
    // run-chaos.ps1 -InjectConfigReloadMs 50 (11 / 55 failures, 5 hosts ×
    // 11 tests: composition state lost mid-word). Defer both the commit
    // AND the engine recreate to ApplyConfigOnHookThread; the hook drain
    // runs them between keystrokes where they're single-writer safe.
    config_.store(std::make_shared<const TypingConfig>(config), std::memory_order_release);
    ApplyConfig(config);
    // Reload `[[hotkeys]]` from TOML alongside main config — keeps registry in
    // sync when Settings dialog persists rebindings via SaveHotkeyRegistry.
    // Still runs migration (idempotent — no-op if section already populated).
    ApplyHotkeyRegistry(ConfigManager::MigrateLegacyHotkeysIfNeeded(
        ConfigManager::GetConfigPath()));

    currentCodeTable_.store(config.codeTable, std::memory_order_release);
    globalCodeTable_.store(config.codeTable, std::memory_order_release);
    globalInputMethod_.store(config.inputMethod, std::memory_order_release);

    // Phase 3d — one helper does it all: TOML parse for overrides /
    // excluded apps / TSF apps / macros, ConfigSnapshot::Build (derives
    // spaceMacroKeys), atomic publish. The re-evaluate block below reads
    // the freshly-published snapshot for the current-app fields.
    RebuildSnapshotFromToml(
        static_cast<std::uint32_t>(lastConfigGeneration_.load(std::memory_order_acquire)));
    auto rcuSnap = configSnapshot_.load(std::memory_order_acquire);

    // Re-evaluate excluded status for current app (set was just reloaded)
    const auto& curExe = focus_.ActiveExe();
    bool newExcluded = false;
    if (config.excludeApps && !curExe.empty() && rcuSnap) {
        newExcluded = rcuSnap->excludedAppSet.count(curExe) > 0;
        isExcludedApp_.store(newExcluded, std::memory_order_release);
    } else {
        newExcluded = isExcludedApp_.load(std::memory_order_acquire);
    }

    // Re-evaluate TSF app status for current foreground app
    const bool wasTsfApp = isTsfApp_.load(std::memory_order_acquire);
    bool newTsfApp;
    if (config.tsfApps && !newExcluded && rcuSnap && !rcuSnap->tsfAppSet.empty() && !curExe.empty()) {
        newTsfApp = rcuSnap->tsfAppSet.count(curExe) > 0;
    } else {
        newTsfApp = false;
    }
    isTsfApp_.store(newTsfApp, std::memory_order_release);
    HOOK_LOG(L"  Engine (config reload): %s for '%s' (tsf_feature=%d, in_tsf_list=%d, excluded=%d)",
             newTsfApp ? L"TSF (hook passthrough)" : L"HOOK",
             curExe.c_str(),
             config.tsfApps ? 1 : 0,
             (rcuSnap && !curExe.empty() && rcuSnap->tsfAppSet.count(curExe) > 0) ? 1 : 0,
             newExcluded ? 1 : 0);
    if (tsfModeCallback_) {
        const bool tsfReadonly = !newTsfApp && !newExcluded;
        if (newTsfApp != wasTsfApp) {
            HOOK_LOG(L"  TSF_ACTIVE flag: %s → %s",
                     wasTsfApp ? L"true" : L"false", newTsfApp ? L"true" : L"false");
        }
        tsfModeCallback_(newTsfApp, tsfReadonly);
    }

    // Re-apply per-app encoding override for current app. Encoding is a
    // plain enum (`CodeTable`) read on the hook hot path without locking;
    // a worker-side write is a torn-read risk but NOT a UAF — minor
    // staleness window only. Acceptable for an enum-sized field.
    if (!curExe.empty() && !newExcluded && !newTsfApp && rcuSnap) {
        auto it = rcuSnap->appEncodingOverrides.find(curExe);
        currentCodeTable_.store(
            (it != rcuSnap->appEncodingOverrides.end())
                ? it->second
                : globalCodeTable_.load(std::memory_order_acquire),
            std::memory_order_release);
    }
    // P3e fix — per-app inputMethod override engine recreate moved to
    // ApplyConfigOnHookThread (same race surface as the unconditional
    // recreate removed above). Worker thread cannot safely swap
    // `engine_` while hook hot path holds raw pointer reads.

    // Notify main process to reload hotkey / QuickConvert configs.
    // Main owns HotkeyManager slots and calls UpdateHotkey there.
    if (configReloadCallback_) {
        configReloadCallback_();
    }

    // P3e fix — post kConfigApply to the hook mailbox so the drain runs
    // ApplyConfigOnHookThread between keystrokes. This is the producer
    // for the dormant handler we wired in P2c — finally lit up. The
    // mailbox coalesces against rapid republishes (one Apply per drain
    // cycle) so chaos `-InjectConfigReloadMs 50` doesn't queue up many.
    lifecycle_.Mailbox().Post(HookCommand::kConfigApply);
}

// ═══════════════════════════════════════════════════════════
// Static Hook Callbacks → Instance Dispatch
// ═══════════════════════════════════════════════════════════

LRESULT CALLBACK HookEngine::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Phase 1: Tier 2 budget marker (<30ms p99). Wraps the full LL callback
    // body so the recorded delta includes every nested stage. PERF_SCOPE
    // compiles to (void)0 when VKEY_PERF_HIST is not defined.
    PERF_SCOPE(::NextKey::Perf::Stage::TotalKeydown);
    // `self` declared outside the try so the catch block can call
    // ResetComposition (Rule 11.5 — "ALWAYS reset state on exception"). Without
    // this, a throw escaping ProcessKeyDown leaves engine_/previousComposition_
    // in a half-updated state for the next keystroke. Re-load is cheap (atomic
    // load) and ResetComposition asserts hook-thread (which we are, here).
    HookEngine* self = s_instance.load(std::memory_order_relaxed);
    // Top-level catch: a C++ throw escaping a low-level hook unwinds through
    // KiUserCallbackDispatcher and Windows raises STATUS_FATAL_USER_CALLBACK_EXCEPTION
    // (0xC000041D), terminating the process. Swallow + log so the next keystroke
    // gets a fresh attempt instead of the app silently disappearing.
    try {
        auto* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Anti-Dorion v2: bump hook-fire counter on every keydown the hook is
        // invoked for — BEFORE any early-return (synthetic VKEY_EXTRA_INFO,
        // sending_, nCode<0). HookHijackDetector's polling compares its
        // observed GetKeyboardState up→down count vs this; counters must move
        // in lockstep so synthetics (which also appear in GetKeyboardState as
        // transient down state) don't cause false-positive bypass detection.
        // Rule 11.2 compliant: single atomic fetch_add on the hot path.
        // See docs/plans/2026-05-28-anti-dorion-detector-inject-design.md §3.
        if (self && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
            self->hookFireCount_.fetch_add(1, std::memory_order_release);
        }

        // Always track our own synthetic events regardless of nCode.
        // When nCode < 0, Windows tells us to pass the message along — but the event
        // still represents a delivered synthetic that was counted when sent.
        // Without this, synthEventsPending_ leaks on every nCode < 0 delivery.
        if (self && pKey->dwExtraInfo == VKEY_EXTRA_INFO) {
            HOOK_LOG(L"  PASSTHRU (dwExtraInfo=NK): vk=0x%02X scan=0x%04X flags=0x%08X nCode=%d",
                     pKey->vkCode, pKey->scanCode, pKey->flags, nCode);
            self->dispatcher_.DecrementSynthEvents();
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        if (nCode == HC_ACTION && self) {
            // Skip events while we're sending (safety backup)
            if (self->dispatcher_.IsSending()) {
                HOOK_LOG(L"  PASSTHRU (sending_): vk=0x%02X scan=0x%04X flags=0x%08X",
                         pKey->vkCode, pKey->scanCode, pKey->flags);
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            // Adaptive-tick — mark every real user key as activity (after
            // synth-event filter at line 758 and sending-state filter above).
            // Rule 11.2 compliant: relaxed atomic store + branch; on idle->
            // active transition fires one workerSignalFn_ call. See plan
            // docs/plans/2026-05-27-adaptive-tick-idle-backoff.md §2.5.
            self->MarkActivity();

            // Rule 11.4 step 5 — drain cross-thread commands BEFORE the
            // English-mode / modifier-key dispatch chain so state mutations
            // posted by main / worker / hotkey threads land before this
            // keystroke is classified. Drain is cheap when nothing is
            // pending (one atomic load + one branch).
            //
            // Placement constraint: MUST come after sending_ (synthetic
            // events from injector_->Replace must not re-enter drain) and
            // BEFORE the English-mode passthrough so an in-flight V/E
            // toggle posted from the hotkey thread takes effect on the
            // very next keystroke, not the one after.
            self->DrainHookCommands();

            bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

            HOOK_LOG(L"KEY vk=0x%02X scan=0x%04X flags=0x%08X %s",
                     pKey->vkCode, pKey->scanCode, pKey->flags,
                     isDown ? L"DOWN" : (isUp ? L"UP" : L"OTHER"));

            // REGRESSION TRAP — DO NOT UNCOMMENT
            //
            // Sprint 1 D4 originally took stateMutex_ here to guard the racing
            // reads of `engine_`, `previousComposition_`, app-detect flags, etc.
            // Phase B (D5-D11) replaced every reader/writer with std::atomic
            // + RCU patterns; the lock is no longer needed and the type
            // (`std::recursive_mutex`) was downgraded to `std::mutex` in D11
            // — uncommenting this line triggers a compile error which IS the
            // intentional regression trap. `tools/audit/check_hook_thread_no
            // _mutex.sh` Check 1 verifies this line stays commented (one of
            // 3 such lines across hook callbacks). If you're tempted to "clean
            // up" the dangling reference, read the audit script first.
            // std::lock_guard<std::recursive_mutex> _lock(self->stateMutex_);

            if (isDown) {
                if (self->ProcessKeyDown(pKey->vkCode, pKey->scanCode, pKey->flags)) {
                    HOOK_LOG(L"  → EATEN (key-down vk=0x%02X)", pKey->vkCode);
                    return 1;  // Eat the keystroke
                }
            } else if (isUp) {
                if (self->ProcessKeyUp(pKey->vkCode, pKey->flags)) {
                    return 1;  // Eat the keystroke
                }
            }
        }
    } catch (const std::exception& e) {
        CrashLog(L"HookEngine::LowLevelKeyboardProc", e.what());
        // Rule 11.5 safety net: an exception escaping ProcessKey* leaves the
        // engine + previousComposition + per-word flags in an undefined state.
        // ResetComposition clears them so the next keystroke starts fresh
        // instead of compounding the corruption.
        if (self) self->ResetComposition();
    } catch (...) {
        CrashLog(L"HookEngine::LowLevelKeyboardProc", "(non-std exception)");
        if (self) self->ResetComposition();
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// Wave 3 PR 3.2 — WinEventProc moved to FocusOwner::WinEventProc. The
// classification-only dispatch (FOREGROUND / MINIMIZEEND → OnFocusChanged)
// runs there; HookEngine's OnFocusChanged shim is wired via the
// FocusChangedFn callback registered in focus_.Install().

LRESULT CALLBACK HookEngine::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    try {
        if (nCode == HC_ACTION && wParam == WM_LBUTTONDOWN) {
            HookEngine* self = s_instance.load(std::memory_order_relaxed);
            if (self) {
                // REGRESSION TRAP — DO NOT UNCOMMENT (see LowLevelKeyboardProc
                // above for the full rationale). Mouse path includes a writer
                // (ResetComposition); torn-read risk pre-Phase-B was higher
                // here than the keyboard read paths. Phase B replaced this
                // with atomic state — current cachedFocusedHwnd_ + Reset-
                // Composition write set is captured as Pre-T3 review Minor
                // 1 in docs/TODO.md (still-open audit). Audit Check 1
                // enforces this line stays commented.
                // std::lock_guard<std::recursive_mutex> _lock(self->stateMutex_);
                HOOK_LOG(L"MOUSE click — resetting composition (engine count=%zu, prev='%s')",
                         self->engine_->Count(), self->previousComposition_.c_str());
                // Always reset, even when engine is idle: commitUndoState_ and commitStack_
                // may hold a previously committed word. If not cleared here, a click elsewhere
                // followed by Backspace triggers ReplayCommittedChars() at the new cursor
                // position — identical to the Ctrl+A bug.
                self->ResetComposition();
                // Click may move focus to another control within the same app (no
                // EVENT_SYSTEM_FOREGROUND fires) — invalidate cache so the next
                // TryEditMessagePaste re-queries the focused HWND.
                self->focus_.InvalidateFocusCache();

                // Anti-Dorion (hook-only): Chromium / Electron / Tauri hosts (e.g.
                // Dorion) install their own WH_KEYBOARD_LL above ours and re-arm
                // mid-session, so the focus-time reinstall (FocusOwner) goes stale
                // and our hook stops firing — "completely can't type, no KEY log".
                // The mouse hook keeps firing though, and the user must click the
                // input field before typing, so a click is the natural moment to
                // reclaim the top of the chain. Throttled (500 ms) in HookLifecycle
                // so rapid clicks don't churn; PostReinstallHooks no-ops if the hook
                // thread isn't running.
                // A1: reclaim-on-click only for a known hijacker (Dorion). Plain
                // browsers don't hijack, so clicking into Chrome shouldn't churn
                // the hook chain.
                if (self->isKnownHijackerApp_.load(std::memory_order_acquire)) {
                    self->lifecycle_.PostReinstallHooks(REINSTALL_REASON_CHROMIUM);
                }
            }
        }
    } catch (const std::exception& e) {
        CrashLog(L"HookEngine::LowLevelMouseProc", e.what());
    } catch (...) {
        CrashLog(L"HookEngine::LowLevelMouseProc", "(non-std exception)");
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// Forward declarations for file-scope helpers used in ProcessKeyDown
static HWND GetInputTarget();
static bool IsIncompatibleLayout(HKL hkl);

// ═══════════════════════════════════════════════════════════
// Core Processing
// ═══════════════════════════════════════════════════════════

bool HookEngine::ProcessKeyDown(DWORD vkCode, DWORD /*scanCode*/, DWORD /*flags*/) {
    // Formula-segment tracking runs first so it observes EVERY key (including
    // ones a guard below eats), keeping the injector's bait-suppress flag in
    // sync with whether we're inside an Excel "=..." cell.
    UpdateFormulaSegment(vkCode);

    // H1b: top-of-pipeline guards extracted to RunTopGuards (steps 0/0b/1/1b/1c).
    // Behavior preserved byte-identical — see method comment for details.
    switch (RunTopGuards(vkCode)) {
        case KeyOutcome::Eat: return true;
        case KeyOutcome::Pass: return false;
        case KeyOutcome::Fallthrough: break;
    }

    // Non-modifier key pressed — invalidate modifier-only hotkey combo (any
    // pending double-tap chain on Ctrl/Shift/Alt/Win is now contaminated)
    otherKeyPressed_ = true;
    for (int i = 0; i < kModCount; ++i) modTapCount_[i] = 0;

    // Watchdog: reset synthEventsPending_ if stuck > 500ms.
    // Covers event loss in Electron/Console multi-process apps where synthetic
    // events can be dropped under heavy CPU load, causing cascading re-injection
    // and ghost characters.
    if (dispatcher_.SynthEventsPending() > 0) {
        DWORD elapsed = GetTickCount() - dispatcher_.LastSynthSendTime();
        if (elapsed > 500) {
            HOOK_LOG(L"  watchdog: synthEventsPending_ reset from %d (stuck %ums)",
                     dispatcher_.SynthEventsPending(), elapsed);
            dispatcher_.ResetSynthEvents();
        }
    }

    // 2c. Fast English exit — skip commit-undo step when no undo is pending.
    //      Commit-undo only applies to Vietnamese words (line 691 checks vietnameseMode_).
    //      When English mode + undo Idle + no English macros → nothing below applies.
    // Sprint 1 D5.2: hoist atomic config-flag loads to a single snapshot at the
    // top of the hot path. Same-thread within ProcessKeyDown — no need to re-load
    // (config writers run on main and cannot interleave a sub-ms hook callback).
    const bool vnMode = vietnameseMode_.load(std::memory_order_acquire);
    const bool macroOn = macroEnabled_.load(std::memory_order_acquire);
    const bool macroEng = macroInEnglish_.load(std::memory_order_acquire);
    if (!vnMode &&
        commitState_.IsIdle() &&
        !(macroOn && macroEng)) {
        return false;
    }

    // Cache key states once per keystroke (GetKeyState is a snapshot, safe to
    // cache). Used by step 2d KeyContext (W4a), HandlePreDispatch (vnMode
    // tracking), and DispatchKeyAction. Moved above step 2d in W4a so the
    // PreEngine pipeline has real modifier flags for EscRestoreRawFeature's
    // hotkey check.
    const bool cachedShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool cachedCapsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    const bool cachedCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool cachedAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool cachedWin = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;

    // 2d. PreEngine pipeline dispatch.
    //   W3: CommitUndoFeature owns the commit-undo FSM at prio 20.
    //   W4a: EscRestoreRawFeature owns hotkey-triggered raw-input restore at prio 40.
    // Coordinator runs features in priority order. Features emit Intents::
    // ConsumeKey (→ return true) or PassThrough (→ return false); empty batch
    // means fall through to step 3+.
    {
        std::wstring_view engineRendered =
            engine_ ? std::wstring_view{engine_->Peek()} : std::wstring_view{};
        std::wstring_view rawSnapshot =
            engine_ ? engine_->PeekRawView() : std::wstring_view{};
        NextKey::Pipeline::HookCompositionSession session(
            previousComposition_, engineRendered, rawSnapshot);
        NextKey::Pipeline::KeyContext keyCtx{
            static_cast<std::uint16_t>(vkCode),
            L'\0',
            cachedShift, cachedCapsLock, cachedCtrl, cachedAlt, cachedWin,
            &session,
            0
        };
        coordinator_.HandleKeyAtStage(
            NextKey::Pipeline::Stage::PreEngine, keyCtx, outputChannel_);
        // DrainBatch (not TakeBatch): empties the channel WITH capacity retained
        // so this per-keystroke drain on the LL-hook thread does not heap-allocate
        // (Rule 11.2). The channel is empty right after the call, so the early
        // `return`s below don't strand intents into the next keystroke.
        const auto& batch = outputChannel_.DrainBatch();
        for (const auto& intent : batch) {
            if (std::holds_alternative<NextKey::Pipeline::Intents::ConsumeKey>(intent))
                return true;
            if (std::holds_alternative<NextKey::Pipeline::Intents::PassThrough>(intent))
                return false;
        }
        // No flow-control intent → Fallthrough: continue to step 3+.
    }

    // H1c: English-mode short-circuit + Vietnamese pre-dispatch tracking
    // (steps 3 / 3a-3d). Behavior preserved byte-identical.
    switch (HandlePreDispatch(vkCode, vnMode,
                              cachedShift,
                              cachedCtrl, cachedAlt, cachedWin)) {
        case KeyOutcome::Eat: return true;
        case KeyOutcome::Pass: return false;
        case KeyOutcome::Fallthrough: break;
    }

    // H1c: action dispatch (steps 4b-10). Returns Eat or Pass for every code path.
    switch (DispatchKeyAction(vkCode, cachedShift, cachedCapsLock, cachedCtrl,
                              cachedAlt, cachedWin, macroOn)) {
        case KeyOutcome::Eat: return true;
        case KeyOutcome::Pass: return false;
        case KeyOutcome::Fallthrough: break;
    }

    return false;
}

// H1b: top-of-pipeline guards extracted from ProcessKeyDown steps 0/0b/1/1b/1c.
//
//  Step 0  — QuickSyncFromSharedState (atomic config epoch; see comment below).
//  Step 0b — TSF early-out: foreground app is in TSF list, hook does nothing.
//  Step 1  — Track modifier keys (LCTRL/RCTRL/LSHIFT/RSHIFT/LMENU/RMENU/LWIN/RWIN);
//            pass through without consumption (don't eat modifier keys themselves).
//  Step 1b — Toggle keys (CapsLock/NumLock/ScrollLock): pass through without
//            committing composition (CapsLock often pressed mid-word).
//  Step 1c — Excluded-app passthrough: same-PID short-circuit; different-PID
//            verifies via VerifyExcludedState; on cleared, NotifyModeChange and
//            fall through to normal processing for this keystroke.
//
// Behavior is byte-identical to the pre-extraction inline block. Returns:
//   Eat         → ProcessKeyDown returns true (no top guards do this today,
//                 reserved for future use).
//   Pass        → ProcessKeyDown returns false (TSF / modifier / toggle /
//                 still-excluded paths).
//   Fallthrough → continue with subsequent ProcessKeyDown steps (only when no
//                 guard matched, or excluded-app cleared its PID).
//
// The post-guard bookkeeping in ProcessKeyDown (otherKeyPressed_=true,
// modTapCount_[]=0, synth-pending watchdog) lives in the wrapper, not here, so it
// runs only on Fallthrough. The excluded-app same-PID and still-excluded paths
// set otherKeyPressed_ themselves before returning Pass, preserving the original
// "any non-modifier key invalidates the modifier-only combo" semantics.
HookEngine::KeyOutcome HookEngine::RunTopGuards(DWORD vkCode) {
    PERF_SCOPE(::NextKey::Perf::Stage::TopGuard);
    // 0. Sync from SharedState. Fast path (post Pre-T3 Minor 2 fix) is
    //    fully lock-free — atomic ReadEpoch + atomic load of lastEpoch_,
    //    early-return on unchanged. Cost ~5 ns. The slow path (taken
    //    only when configGeneration bumped — user-paced Settings save,
    //    not chaos) acquires stateMutex_ + may run ReloadFromToml on
    //    this thread; that residual Rule #11.2 cost is bounded to one
    //    reload per generation bump (~10–50 ms once / minute of user
    //    config tweaking). Steady-state typing never reaches it.
    QuickSyncFromSharedState();

    // 0b. TSF app — let TSF DLL handle all input, hook does nothing
    if (isTsfApp_.load(std::memory_order_acquire)) return KeyOutcome::Pass;

    // 1. Track modifiers for hotkey detection
    bool isModifier = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
                       vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
                       vkCode == VK_LMENU || vkCode == VK_RMENU ||
                       vkCode == VK_LWIN || vkCode == VK_RWIN);

    if (isModifier) {
        TrackModifier(vkCode, true);
        return KeyOutcome::Pass;  // Don't eat modifier keys
    }

    // 1b. Toggle keys (CapsLock, NumLock, ScrollLock) — pass through without
    // committing composition. CapsLock is commonly pressed mid-word to capitalize
    // the first letter of a Vietnamese word (e.g., CapsLock+G+CapsLock+iar → Giả).
    // Without this bypass, CapsLock would hit step 9 ("any other key → commit"),
    // splitting the word and producing wrong tone placement (Gỉa instead of Giả).
    if (vkCode == VK_CAPITAL || vkCode == VK_NUMLOCK || vkCode == VK_SCROLL) {
        return KeyOutcome::Pass;
    }

    // 1c. Excluded app — full passthrough (IME is transparent to this app)
    // Fast PID check: same process → passthrough immediately (no syscall overhead).
    // Different PID → verify with full exe name lookup (only on actual app switch).
    if (isExcludedApp_.load(std::memory_order_acquire)) {
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == excludedPid_.load(std::memory_order_acquire)) {
            otherKeyPressed_ = true;
            return KeyOutcome::Pass;  // Same process — still excluded
        }
        // Different process — verify if we actually left the excluded app
        if (VerifyExcludedState()) {
            excludedPid_.store(fgPid, std::memory_order_release);  // Switched to another excluded app
            otherKeyPressed_ = true;
            return KeyOutcome::Pass;
        }
        excludedPid_.store(0, std::memory_order_release);
        NotifyModeChange();
        // Fall through to normal processing for this keystroke
    }

    return KeyOutcome::Fallthrough;
}

// H1a: commit-undo state machine extracted from ProcessKeyDown step 2d.
// Supports multi-word backward — stack holds up to CommitState::kMaxStack committed words.
// Ready:  set after commit with space/enter, or when engine empties after BS with stack non-empty.
// Primed: BS in Ready deletes the space; next alpha/BS triggers replay.
//
// Behavior is byte-identical to the pre-extraction inline block. Returns:
//   Eat         → ProcessKeyDown returns true (key consumed by undo machinery).
//   Pass        → ProcessKeyDown returns false (key passes through to app).
//   Fallthrough → no decision; ProcessKeyDown continues with subsequent steps.
HookEngine::KeyOutcome HookEngine::HandleCommitUndoFsm(DWORD vkCode, bool vnMode) {
    // Ctrl/Alt/Win invalidate commit-undo: Ctrl+BS deletes entire word (not just the
    // space), Ctrl+A/C/Z change cursor/selection — all make saved commit state stale.
    // Must check BEFORE the state machine to prevent ghost key replay.
    if (!commitState_.IsIdle() &&
        ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
         (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))) {
        HOOK_LOG(L"  commit-undo: cancel — modifier key held");
        CancelCommitUndo();
        // Fall through — Ctrl check at ProcessKeyDown step 5 will handle ResetComposition
    }

    // Enter (VK_RETURN) doesn't move focus in chat/form inputs — cursor
    // stays in the same input box on message-send / line-break. That
    // bypasses the focus-event safety net that Tab + mouse-click rely on
    // to clear commitStack_ via ResetComposition. Without this explicit
    // cancel, the stack survives across message-send boundaries: user
    // sends "không," then starts a new message with "vaf"; the BS chain
    // they use to correct a typo in the new message replays the phantom
    // "không" prefix into the engine; IsHardEnglishToneContext sees the
    // concat-buffer V-CC-V pattern and blocks tone on the new word.
    // Existing line ~1319 already cancels Enter while state==Ready;
    // this branch guards the gap where a prior non-exempt key (e.g.
    // SPACE) downgraded state to Idle but left the stack populated.
    // Idempotent — no-op when state==Idle && stack already empty.
    if (vkCode == VK_RETURN &&
        (!commitState_.IsIdle() || !commitState_.StackEmpty())) {
        HOOK_LOG(L"  commit-undo: cancel — VK_RETURN (stack=%zu state=%d)",
                 commitState_.StackSize(), static_cast<int>(commitState_.Current()));
        CancelCommitUndo();
        // Fall through — Enter still passes through to the app normally.
    }
    //
    // Auto-expire Ready after kCommitUndoTimeoutMs: cheap insurance against any cursor-movement
    // event that bypasses ResetComposition (e.g. external text change, rare edge cases).
    if (commitState_.IsReady()) {
        DWORD elapsed = GetTickCount() - commitState_.ReadyTime();
        if (elapsed > kCommitUndoTimeoutMs) {
            HOOK_LOG(L"  commit-undo: Ready state expired after %u ms → Idle", elapsed);
            CancelCommitUndo();
        }
    }
    if (commitState_.IsReady() && vkCode == VK_BACK && engine_->Count() == 0) {
        if (commitState_.PendingTriggerCount() > 0) {
            // Extra trigger chars still on screen (e.g., "a==" → need to delete both '=' before undo)
            commitState_.DecrementPendingTriggers();
            HOOK_LOG(L"  commit-undo: BS in Ready, pendingTriggers=%u — stay Ready", commitState_.PendingTriggerCount());
            return KeyOutcome::Pass;  // Let BS pass through to delete the extra trigger char
        }
        // Backspace deletes the commit trigger (space/etc.)
        commitState_.SetPrimed();
        // Any accumulated multi-word-macro state is stale once replay begins —
        // the phrase buffer no longer mirrors what's on screen.
        macroCrossCommit_ = false;
        rawMacroBuffer_.clear();
        if (dispatcher_.SynthEventsPending() > 0) {
            // Synthetic events still in flight (word corrections, injected commit trigger).
            // If we pass BS through now it arrives at the app BEFORE those synthetics,
            // deleting the wrong character and permanently desynchronising previousComposition_.
            // Re-inject so BS is placed AFTER the pending synthetics in the queue.
            HOOK_LOG(L"  commit-undo: BS after commit → Primed, re-inject after synthetics (pending=%d)", dispatcher_.SynthEventsPending());
            InjectKey(VK_BACK);
            return KeyOutcome::Eat;
        }
        // Sprint 1 Fix C/2026-05-05: editMsg apps need this BS via the sent
        // EM_REPLACESEL channel — passing the physical BS through goes via the
        // posted message queue and is pre-empted by the next sent EM_REPLACESEL
        // (the 's' in chaos 5.3), leaving the pre-replace BS to drain after
        // the replacement and eat the just-inserted chars.
        //
        // The synchronous-channel injector (RichEditEm) handles commit-undo BS
        // via sent message. Default hosts let physical BS pass through naturally —
        // synthesizing would just add latency.
        if (dispatcher_.IsSyncReplaceChannel()) {
            auto inj = dispatcher_.GetInjector();
            bool injOk;
            { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
              injOk = inj->Replace(/*bs=*/1, std::wstring_view{}); }
            if (injOk) {
                HOOK_LOG(L"  commit-undo: BS after commit via injector → Primed");
                return KeyOutcome::Eat;
            }
            HOOK_LOG(L"  commit-undo: BS after commit injector failed, passthrough");
        }
        HOOK_LOG(L"  commit-undo: BS after commit → Primed (ready to replay)");
        return KeyOutcome::Pass;  // Let backspace pass through to delete the space
    }
    if (commitState_.IsPrimed() && engine_->Count() == 0 && vnMode) {
        // Synth guard: if synthetic events were sent recently and are likely still
        // in the OS input queue, replaying now would set previousComposition_ to stale
        // committed text while the screen hasn't caught up — causing diff miscalculation
        // and permanent engine-screen desync.  Cancel commit-undo and fall through to
        // normal key processing.
        // Time check is essential: on Qt apps, synthEventsPending_ has a persistent
        // baseline leak (counter never reaches 0 due to event counting mismatch).
        // Checking counter alone would permanently disable commit-undo.  The 100ms
        // threshold covers DispatchSendInput Sleep (10-20ms) + Qt processing (~30ms)
        // with margin, while allowing replay at normal typing speed (>100ms between keys).
        //
        // Sprint 2 D1/2026-05-05: tone modifiers (Telex s/f/r/x/j; VNI 1-5) are
        // EXEMPT from the synth guard. Reason: by definition they only modify the
        // previous word — no other linguistic meaning. ReplaceComposition's diff
        // (prev=committed, new=committed-with-tone) computes BS correctly relative
        // to the post-drain screen state, and SendInput appends our events AFTER
        // any pending synth, so screen-engine sync is preserved across the gap.
        // Without this exemption, chaos 5.3 (`viejtnam BS×4 s` on non-EditMsg apps
        // like Chrome) cancels the replay and produces `việts` instead of `viết`.
        // See docs/baselines/perf-baseline-d12-chrome-cross-app.md and the
        // S2D0_ChromeBug53_* engine-isolation tests.
        // Exemption rule shared by the synth-guard and catch-all cancel
        // branches: modifier letters (Telex/SimpleTelex/Combined
        // s/f/r/x/j/z/a/e/o/w/d), VNI digits 0-9, UserDefined keys whose
        // customKeyMap action passes IsCommitUndoExemptAction, and ESC
        // restore-raw all semantically "modify the previous word" — they
        // must not demote / cancel commit-undo state. Extracted to
        // core/CommitUndoExemption.h for Linux GTest coverage (HookEngine.cpp
        // is Win32-only). See design 2026-05-17 + 2026-05-21b broadening.
        const auto methodForExempt = currentMethod_.load(std::memory_order_acquire);
        const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // Source of truth: registry snapshot — Esc only exempts when bound to
        // CancelComposition AND the intent is enabled. Removed in v3 cleanup:
        // legacy `escRestoreRawEnabled_` atomic. Matches() rejects modifier-vk
        // on DOWN so passing keyUp=false is safe for plain Esc.
        const auto hotkeysForExempt = hotkeys_.load(std::memory_order_acquire);
        const bool escIsCancelTrigger =
            hotkeysForExempt &&
            hotkeysForExempt->Matches(Intent::CancelComposition, VK_ESCAPE,
                                       /*mods=*/0, /*isDoubleTap=*/false,
                                       /*keyUp=*/false);
        // UserDefined modifier lookup: customKeyMap can bind any key to
        // a tone/modifier action, so the hardcoded letter/digit lists
        // don't apply. Resolve vk → ASCII via VkToMacroChar (same path
        // step 6d uses) and ask IsCommitUndoExemptAction whether the
        // mapped action belongs to the "modifies previous word" class.
        bool isCustomModifier = false;
        if (methodForExempt == InputMethod::UserDefined) {
            const wchar_t ch = VkToMacroChar(vkCode);
            if (ch && ch < 128) {
                const TypingAction action =
                    config_.load(std::memory_order_acquire)
                        ->customKeyMap[static_cast<uint8_t>(ch)];
                isCustomModifier = IsCommitUndoExemptAction(action);
            }
        }
        const bool isCommitUndoExempt = IsCommitUndoExemptKey(
            vkCode, methodForExempt, shiftHeld, escIsCancelTrigger,
            isCustomModifier);
        // Sprint 2 D5: settle window is now per-host. RichEdit (0 ms) lets
        // commit-undo replay immediately; Win32 (30 ms) tightens the gate
        // ~3× vs the legacy 100 ms hardcode; Electron/Console (100 ms) keeps
        // the original budget where IPC reorder margin still matters. Read
        // here, not cached, so a focus change between commit and the next
        // BS uses the new injector's budget.
        const DWORD settleMs = static_cast<DWORD>(
            dispatcher_.GetInjector()->SettleBudget().count());
        if (dispatcher_.SynthEventsPending() > 0 && (GetTickCount() - dispatcher_.LastRealSynthTime()) < settleMs
            && !isCommitUndoExempt) {
            HOOK_LOG(L"  commit-undo: cancel Primed — synthPending=%d, vk=0x%02X",
                     dispatcher_.SynthEventsPending(), vkCode);
            CancelCommitUndo();
            // Fall through — ProcessKeyDown step 10 re-injects BS if needed; alpha → step 6 HandleAlphaKey
        } else if (vkCode >= 0x41 && vkCode <= 0x5A) {
            // Discriminate alpha intent at Primed: tone modifier (Telex s/f/r/x/j,
            // per IsCommitUndoExemptKey — same "modifies previous word" semantic
            // class used by synth-guard and catch-all branches) → REPLAY. Other
            // alphas → user typing new word after BS-chain navigated past the
            // committed word; DROP stack-top to prevent a later BS-into-empty
            // from re-priming Ready for it, and fall through so the alpha enters
            // fresh composition. Without this, catch-all replay concatenated an
            // older stack entry into the new word (engine/screen divergence).
            if (!isCommitUndoExempt) {
                HOOK_LOG(L"  commit-undo: drop stack-top '%s' for non-tone alpha '%c' → fresh composition",
                         commitState_.StackEmpty() ? L"<empty>" : commitState_.StackTop().text.c_str(),
                         static_cast<char>(vkCode));
                if (!commitState_.StackEmpty()) {
                    commitState_.PopStackTop();
                }
                commitState_.SetIdle();
                return KeyOutcome::Fallthrough;
            }
            // MUST return HandleAlphaKey's value: if it triggers passthrough (return false),
            // the original key must reach the app — ignoring it would swallow the keystroke.
            HOOK_LOG(L"  commit-undo: replaying + tone-alpha '%c' (stack_top='%s' stackSize=%zu prevComp='%s' synthPending=%d)",
                     static_cast<char>(vkCode),
                     commitState_.StackEmpty() ? L"<empty>" : commitState_.StackTop().text.c_str(),
                     commitState_.StackSize(),
                     previousComposition_.c_str(),
                     dispatcher_.SynthEventsPending());
            ReplayCommittedChars();
            {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool caps = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                return HandleAlphaKey(vkCode, shift, caps)
                    ? KeyOutcome::Eat
                    : KeyOutcome::Pass;
            }
        } else if (const InputMethod method = currentMethod_.load(std::memory_order_acquire);
                   (method == InputMethod::VNI || method == InputMethod::Combined ||
                    method == InputMethod::UserDefined) &&
                   vkCode >= 0x30 && vkCode <= 0x39 &&
                   !(GetKeyState(VK_SHIFT) & 0x8000)) {
            // VNI/Combined/UserDefined digit key (0-9) → replay saved chars, then process
            // as tone/modifier. Without this, "cá " + BS + '2' would produce "cá2" instead
            // of "cà". '0' is VNI clear-tone; UserDefined may map any digit via customKeyMap.
            HOOK_LOG(L"  commit-undo: replaying + VNI digit '%c' (stack_top='%s' stackSize=%zu prevComp='%s')",
                     static_cast<char>(vkCode),
                     commitState_.StackEmpty() ? L"<empty>" : commitState_.StackTop().text.c_str(),
                     commitState_.StackSize(),
                     previousComposition_.c_str());
            ReplayCommittedChars();
            if (engine_->Count() == 0) {
                commitState_.SetIdle();
                return KeyOutcome::Pass;  // Replay failed — let digit pass through
            }
            return HandleVniDigitKey(vkCode)
                ? KeyOutcome::Eat
                : KeyOutcome::Pass;
        } else if (vkCode == VK_BACK) {
            // Backspace → replay saved chars, then backspace into the word
            HOOK_LOG(L"  commit-undo: replaying + backspace (stack_top='%s' stackSize=%zu prevComp='%s' synthPending=%d)",
                     commitState_.StackEmpty() ? L"<empty>" : commitState_.StackTop().text.c_str(),
                     commitState_.StackSize(),
                     previousComposition_.c_str(),
                     dispatcher_.SynthEventsPending());
            ReplayCommittedChars();
            HandleBackspace();
            return KeyOutcome::Eat;
        } else if (!isCommitUndoExempt) {
            // Any other key → cancel commit-undo.
            // Exempt keys (tone modifiers, ESC restore-raw) keep state Primed
            // so the downstream replay / restore handlers can read commitStack_.
            commitState_.SetIdle();
        }
    }
    if (commitState_.IsReady()) {
        // Navigation keys move cursor → stack entries become stale, clear everything.
        if ((vkCode >= VK_LEFT && vkCode <= VK_DOWN) ||
            vkCode == VK_HOME || vkCode == VK_END ||
            vkCode == VK_PRIOR || vkCode == VK_NEXT ||
            vkCode == VK_DELETE) {
            HOOK_LOG(L"  commit-undo: cancel — navigation key vk=0x%02X", vkCode);
            CancelCommitUndo();
        } else if (IsCommitTrigger(vkCode) && engine_->Count() == 0) {
            // Printable commit trigger with engine empty (e.g., second '=' in "a==",
            // second ' ' in "a  "): stay Ready so subsequent BS sequence can reach Primed.
            // `>=` (not `>`) keeps SPACE in the printable branch — MapVirtualKeyW(VK_SPACE)
            // returns L' ', which would otherwise fall into the cancel branch.
            wchar_t ch = VkToMacroChar(vkCode);
            if (ch >= L' ') {
                commitState_.IncrementPendingTriggers();
                HOOK_LOG(L"  commit-undo: extra trigger '%c' in Ready, pendingTriggers=%u", ch, commitState_.PendingTriggerCount());
            } else {
                // Non-printable trigger (Esc, Tab, Enter) → cancel undo
                CancelCommitUndo();
            }
        } else {
            // Alpha, digit, or other key → start new word, preserve stack for multi-word backward.
            // Carry the pending trigger count onto the in-progress word so it travels with the
            // CommitEntry when the word commits — without this, "chịu :D " then BS×3 + 'a' would
            // forget the ':' and cause engine/screen desync (replay fires before ':' is deleted).
            commitState_.SetLeadingTriggersForCurrentWord(commitState_.PendingTriggerCount());
            commitState_.ResetPendingTriggers();
            commitState_.SetIdle();
        }
    }
    return KeyOutcome::Fallthrough;
}

// H1c: English-mode short-circuit + Vietnamese pre-dispatch tracking
// (extracted from ProcessKeyDown steps 3 / 3a-3d).
//
//   Step 3   — !vnMode early-out with English-mode macro tracking. Macro
//              keys accumulate in rawMacroBuffer_; commit triggers attempt
//              expansion; Esc with empty buffer arms tempMacroOff_; non-
//              alpha non-trigger keys clear the buffer at word boundary.
//   Step 3a  — Auto-caps state machine (Idle/AfterPunct/ReadyToCapitalize).
//              Punctuation '.', '?', '!' arms AfterPunct; subsequent space
//              promotes to ReadyToCapitalize; Enter also promotes.
//   Step 3b  — Macro tracking on the Vietnamese path: alpha keys lower-cased
//              (or upper-cased per shift XOR caps), printable triggers join
//              the buffer for multi-char macro key matching.
//   Step 3c  — Temp-off-by-Esc: Esc with engine empty + buffer empty arms
//              tempMacroOff_ for the next word.
//   Step 3d  — Macro expansion on commit trigger via TryExpandMacro.
//
// Behavior is byte-identical to the pre-extraction inline block. Returns:
//   Eat         → ProcessKeyDown returns true (macro expansion ate trigger).
//   Pass        → ProcessKeyDown returns false (English-mode passthrough,
//                 ExpandedPassTrigger without synth, or Esc temp-off arming).
//   Fallthrough → continue to DispatchKeyAction (vnMode + no expansion).
HookEngine::KeyOutcome HookEngine::HandlePreDispatch(DWORD vkCode, bool vnMode,
                                                      bool cachedShift,
                                                      bool cachedCtrl, bool cachedAlt,
                                                      bool cachedWin) {
    // Snapshot the user's hotkey registry once for this key event. RCU
    // pattern: hot path readers grab the shared_ptr; the publisher
    // (ApplyHotkeyRegistry) replaces the pointer without invalidating
    // in-flight readers.
    const auto hotkeysSnap = hotkeys_.load(std::memory_order_acquire);
    // Note: configSnapshot_ + macroTable were read here pre-W4b to gate the
    // inline macro blocks. Those moved to MacroFeature (W4b) at step 2d so
    // the snapshot load is no longer needed in HandlePreDispatch.
    const uint32_t currentMods = ComputeModMask(cachedCtrl, cachedShift, cachedAlt, cachedWin);

    // 3. English mode — skip Vietnamese processing.
    // EN-mode macro tracking + dispatch is now owned by MacroFeature (W4b)
    // at PreEngine step 2d. If the feature consumed/passed the key (Eat/Pass
    // outcomes), ProcessKeyDown returned before reaching HandlePreDispatch.
    // Reaching here means EN mode with no macro engagement — just pass through.
    if (!vnMode) {
        HOOK_LOG(L"  skip: Vietnamese mode OFF");
        return KeyOutcome::Pass;
    }

    // 3a. Auto-caps state machine (Vietnamese mode only). Rule + modifier gate
    // live in core/AutoCapStateTransition.h — Ctrl+Enter / Ctrl+. / Win+. etc.
    // are passed through unchanged so the dispatcher's step 5 modifier guard
    // can reset composition without first arming ReadyToCapitalize.
    if (autoCaps_.load(std::memory_order_acquire)) {
        autoCapState_ = ComputeAutoCapStateTransition(
            autoCapState_, vkCode, cachedShift, cachedCtrl, cachedAlt, cachedWin);
    }

    // 3b. Macro tracking moved to MacroFeature (W4b) at PreEngine step 2d.
    // Pre-W4b accumulated alpha + commit-trigger chars into rawMacroBuffer_ here;
    // now the executor adapter HookEngine::HandleMacro owns that mutation.

    // 3b'. Esc-restore-raw: handled at PreEngine step 2d by EscRestoreRawFeature
    // (Wave 4a). If ESC matched the CancelComposition hotkey and had live/primed
    // composition, the feature emitted Intents::ConsumeKey and ProcessKeyDown
    // returned true before reaching HandlePreDispatch. If we got here, ESC did
    // not match (or no composition was active) — fall through to ToggleEnabled.
    // Post-W4a: MOD-CANCEL secondary site at line 1981 (modifier-release path)
    // still calls TryEscRestoreRaw inline — different trigger flow.

    // 3b''. ToggleEnabled — non-modifier binding (F-key, letter+chord, Esc+mods…)
    // fires on DOWN with exact mods match. Modifier-bound ToggleEnabled lives
    // in ProcessKeyUp's modifier-release dispatch (modifier-alone / double-tap
    // detection). The IsModifierKey gate avoids double-firing for modifier vk.
    // Double-tap on non-modifier keys is currently NOT tracked by HookEngine
    // (modTapCount_ only covers Ctrl/Shift/Alt/Win) — bindings with
    // doubleTap=true on non-modifier vk are accepted by the Hotkeys UI but
    // never fire here. Acceptable v3 limitation; track via Matches() with
    // isDoubleTap=false so only single-tap triggers match.
    if (!IsModifierKey(CanonicalModifierVk(vkCode))
        && hotkeysSnap->Matches(Intent::ToggleEnabled, vkCode, currentMods,
                                 /*isDoubleTap=*/false, /*keyUp=*/false)) {
        if (engine_->Count() > 0) CommitComposition();
        tempEngineOff_ = !tempEngineOff_;
        CancelCommitUndo();
        HOOK_LOG(L"  TOGGLE-DOWN (vk=0x%02X mods=0x%02X): tempEngineOff_=%d",
                 vkCode, currentMods, tempEngineOff_ ? 1 : 0);
        return KeyOutcome::Eat;
    }

    // 3c, 3d. SkipMacro hotkey + macro expansion — owned by MacroFeature
    // (W4b) at PreEngine step 2d. Reaching this point means the feature
    // returned Fallthrough/NoOp (no macro engagement); fall through to the
    // post-HandlePreDispatch dispatcher (step 4+).
    return KeyOutcome::Fallthrough;
}

// H1c: action dispatch chain (extracted from ProcessKeyDown steps 4b-10).
//
//   Step 4b — tempEngineOff_ bypass: vnMode is ON but temporarily disabled
//             for current word (commit trigger or BS-on-empty resets it).
//   Step 5  — Ctrl/Alt/Win shortcut skip: ResetComposition + passthrough.
//   Step 6  — A-Z alpha key → HandleAlphaKey (returns Eat if engine consumed
//             the key, Pass if it triggered passthrough mid-word).
//   Step 6b — Telex bracket [/] → engine modifier for ơ/ư.
//   Step 6c — VNI/Combined digit 1-9 with engine non-empty → HandleVniDigitKey.
//   Step 6d — UserDefined OEM punctuation bound via customKeyMap → engine PushChar.
//   Step 7  — Backspace with engine non-empty → HandleBackspace + Eat.
//   Step 7b — Backspace with cross-commit macro buffer: update tracking,
//             pass through (no return — falls into step 8/9/10).
//   Step 8  — Commit trigger with engine non-empty: macro buffer preservation
//             across the commit, trigger re-injection after pending synth,
//             RichEdit synchronous-channel routing.
//   Step 9  — Any other key with engine non-empty → commit + InjectKey.
//   Step 10 — Backspace with engine empty + synth pending → re-inject BS to
//             preserve ordering after in-flight word corrections.
//
// Behavior is byte-identical to the pre-extraction inline block. Returns:
//   Eat  → ProcessKeyDown returns true (key consumed).
//   Pass → ProcessKeyDown returns false (passthrough — final fallthrough also
//          maps here; the original code's tail `return false` is preserved).
HookEngine::KeyOutcome HookEngine::DispatchKeyAction(DWORD vkCode, bool cachedShift,
                                                      bool cachedCapsLock, bool cachedCtrl,
                                                      bool cachedAlt, bool cachedWin, bool macroOn) {
    // 4b. Temp-off bypass: Vietnamese mode is ON but temporarily disabled for current word
    if (tempEngineOff_) {
        if (IsCommitTrigger(vkCode)) {
            tempEngineOff_ = false;
            digitLedWord_ = false;  // Word ended — digit-led state is moot
            HOOK_LOG(L"  tempEngineOff: reset on commit trigger vk=0x%02X", vkCode);
        } else if (vkCode == VK_BACK && engine_->Count() == 0) {
            tempEngineOff_ = false;
            digitLedWord_ = false;
            HOOK_LOG(L"  tempEngineOff: reset on backspace (engine empty)");
        }
        HOOK_LOG(L"  skip: tempEngineOff_ active=%d", tempEngineOff_ ? 1 : 0);
        return KeyOutcome::Pass;  // Pass through as English
    }

    // 5. Skip if Ctrl/Alt/Win is down (allow shortcuts to pass through)
    if (cachedCtrl || cachedAlt || cachedWin) {
        HOOK_LOG(L"  skip: modifier held (ctrl=%d alt=%d win=%d)", cachedCtrl, cachedAlt, cachedWin);
        // Always reset — shortcuts change text state in unpredictable ways.
        // Commit-undo is already canceled at step 2d (modifier guard), but
        // ResetComposition also clears engine, previousComposition_, inputHistory_, etc.
        // ResetComposition → ClearWordState also drops digit-led state.
        ResetComposition();
        return KeyOutcome::Pass;
    }

    // 5b. Digit-led word state machine: arm on digit at word start (VNI/Combined/
    // UserDefined), bypass while armed, reset on whitespace/nav/Esc/BS/Delete.
    // Single source of truth in core/DigitLedWordDecision.h.
    {
        DigitLedInputs in{
            vkCode, cachedShift,
            engine_->Count() == 0,
            currentMethod_.load(std::memory_order_acquire),
            digitLedWord_,
        };
        switch (DecideDigitLed(in)) {
            case DigitLedDecision::Arm:
                digitLedWord_ = true;
                HOOK_LOG(L"  digitLedWord: armed by vk=0x%02X", vkCode);
                return KeyOutcome::Pass;
            case DigitLedDecision::Bypass:
                HOOK_LOG(L"  digitLedWord: bypass (active)");
                return KeyOutcome::Pass;
            case DigitLedDecision::Reset:
                digitLedWord_ = false;
                HOOK_LOG(L"  digitLedWord: reset on vk=0x%02X", vkCode);
                return KeyOutcome::Pass;
            case DigitLedDecision::Continue:
                break;
        }
    }

    // 6. A-Z keys → process with engine
    if (vkCode >= 0x41 && vkCode <= 0x5A) {
        HOOK_LOG(L"  alpha key '%c' → HandleAlphaKey", static_cast<char>(vkCode));
        return HandleAlphaKey(vkCode, cachedShift, cachedCapsLock)
            ? KeyOutcome::Eat
            : KeyOutcome::Pass;
    }

    const InputMethod method = currentMethod_.load(std::memory_order_acquire);

    // 6b. Bracket keys [ ] → engine modifier for Full Telex ([ → ơ, ] → ư)
    if (method == InputMethod::Telex &&
        (vkCode == VK_OEM_4 || vkCode == VK_OEM_6)) {
        if (!cachedShift) {
            wchar_t ch = (vkCode == VK_OEM_4) ? L'[' : L']';
            commitState_.AppendHistory(ch);
            std::wstring composition;
            { PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
              engine_->PushChar(ch); composition = engine_->Peek(); }
            HOOK_LOG(L"  bracket '%c' → Peek()='%s'", ch, composition.c_str());
            DispatchCoordinator(vkCode, 0, composition);
            return KeyOutcome::Eat;  // Eat the original keystroke
        }
    }

    // 6c. VNI/Combined/UserDefined: digit keys 0-9 → tone/modifier input (only with
    // pending composition). VNI '0' clears tone; UserDefined may remap any digit via
    // customKeyMap (unmapped digits fall through as ProcessChar literal inside engine).
    // The "digit at word start with engine empty" case is already armed and returned
    // at step 5b above; this branch only sees mid-word digits.
    if ((method == InputMethod::VNI || method == InputMethod::Combined ||
         method == InputMethod::UserDefined) &&
        vkCode >= 0x30 && vkCode <= 0x39 &&
        engine_->Count() > 0) {
        if (!cachedShift) {
            return HandleVniDigitKey(vkCode) ? KeyOutcome::Eat : KeyOutcome::Pass;
        }
    }

    // 6d. UserDefined: OEM punctuation bound via customKeyMap → tone/modifier
    // input. Without this branch OEM keys hit step 8 IsCommitTrigger first and
    // never reach engine_->PushChar, so e.g. customKeyMap[';'] = ToneDot would
    // be dead. UserDefined-only by design — VNI/Combined keep digit-only reach.
    //
    // Empty-buffer gate: tone/modifier actions need an existing vowel target,
    // so we keep them mid-word-only. Insert-type actions (HornInsertO/U,
    // Insert*, HornOrInsertU plain) synthesise fresh state and MUST fire at
    // word start too — user feedback 2026-05-17: `[`/`]` bound to HornInsertO/U
    // produced literal `[`/`]` instead of ơ/ư at word start.
    if (method == InputMethod::UserDefined && IsOemPunctVk(vkCode)) {
        const wchar_t ch = VkToMacroChar(vkCode);
        if (ch && ch < 128) {
            auto cfg = config_.load(std::memory_order_acquire);
            const TypingAction action = cfg->customKeyMap[static_cast<uint8_t>(ch)];
            if (action != TypingAction::None &&
                (engine_->Count() > 0 || IsInsertTypeAction(action))) {
                commitState_.AppendHistory(ch);
                std::wstring composition;
                { PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
                  engine_->PushChar(ch); composition = engine_->Peek(); }
                HOOK_LOG(L"  UserDefined OEM '%c' → Peek()='%s'", ch, composition.c_str());
                DispatchCoordinator(vkCode, 0, composition);
                return KeyOutcome::Eat;
            }
        }
    }

    // 7. Backspace → engine backspace if we have content
    if (vkCode == VK_BACK && engine_->Count() > 0) {
        if (macroOn && !rawMacroBuffer_.empty()) rawMacroBuffer_.pop_back();
        HOOK_LOG(L"  backspace (engine count=%zu)", engine_->Count());
        HandleBackspace();
        return KeyOutcome::Eat;  // Eat backspace
    }

    // 7b. Backspace with cross-commit macro buffer: update tracking, pass through
    if (vkCode == VK_BACK && macroCrossCommit_ && !rawMacroBuffer_.empty()) {
        rawMacroBuffer_.pop_back();
        if (rawMacroBuffer_.empty()) macroCrossCommit_ = false;
    }

    // 8. Commit triggers: space, enter, tab, punctuation, numbers, escape, arrows
    if (IsCommitTrigger(vkCode) && engine_->Count() > 0) {
        HOOK_LOG(L"  commit trigger vk=0x%02X", vkCode);

        // Preserve macro buffer across commit for printable triggers (e.g., '.' in "a.i")
        // so macros with punctuation in their key can still be matched on the final trigger.
        // Also preserve across SPACE when the accumulated prefix matches a stored space-
        // containing key — enables multi-word macros like "oc om bok" = "Óoc Om Bok".
        std::wstring savedMacroBuffer;
        // Phase 3c: macro presence + spaceMacroKeys come from the RCU
        // snapshot. Local shared_ptr keeps both alive through the branch.
        auto cfgSnap = configSnapshot_.load(std::memory_order_acquire);
        if (macroOn && cfgSnap && !cfgSnap->macroTable.empty()
            && !tempMacroOff_ && !rawMacroBuffer_.empty()) {
            wchar_t ch = VkToMacroChar(vkCode);
            if (ch > L' ') {
                savedMacroBuffer = rawMacroBuffer_;
            } else if (ch == L' '
                       && IsSpaceMacroPrefix(rawMacroBuffer_ + L' ',
                                             cfgSnap->spaceMacroKeys)) {
                savedMacroBuffer = rawMacroBuffer_ + L' ';
            }
        }

        bool restored = CommitComposition();

        if (!savedMacroBuffer.empty()) {
            rawMacroBuffer_ = std::move(savedMacroBuffer);
            macroCrossCommit_ = true;
        }
        // Enable backspace-into-word for printable commit triggers (space, enter,
        // digits, punctuation). Navigation keys (arrows, Tab, ESC, etc.) move the
        // cursor — replay would insert text at the wrong position, so exclude them.
        // Only if a new entry was just pushed (implies: not auto-restored,
        // not quick consonant, not empty history).
        if (commitState_.PushedToStack()) {
            bool isNavigation = (vkCode >= VK_LEFT && vkCode <= VK_DOWN) ||
                vkCode == VK_HOME || vkCode == VK_END ||
                vkCode == VK_PRIOR || vkCode == VK_NEXT ||
                vkCode == VK_TAB || vkCode == VK_ESCAPE ||
                vkCode == VK_DELETE || vkCode == VK_INSERT;
            if (!isNavigation) {
                SetCommitUndoReady();
            }
        }
        if (restored || dispatcher_.SynthEventsPending() > 0) {
            // Re-inject trigger AFTER all pending synthetic events so that:
            //   (a) auto-restore replacement arrives before the trigger, and
            //   (b) in-flight correction synthetics (e.g. from ee→ê mid-word) arrive
            //       before the trigger — preventing the trigger from slipping ahead of
            //       those backspaces/chars and causing corrupt output ("lỗiêhiênr").
            HOOK_LOG(L"  re-inject trigger vk=0x%02X (restored=%d synthPending=%d)",
                     vkCode, restored ? 1 : 0, dispatcher_.SynthEventsPending());
            InjectKey(vkCode);
            return KeyOutcome::Eat;  // Eat original trigger
        }
        // Sprint 1 Fix C/2026-05-05: in async-render hosts (Win11 New Notepad
        // RichEditD2DPT) every alpha key is now routed through EM_REPLACESEL
        // (sent message). A passthrough trigger char arrives via posted
        // WM_KEYDOWN, and sent messages pre-empt posted ones — so the next
        // eaten alpha's EM_REPLACESEL can be processed before the previous
        // word's space/punctuation makes it to WM_CHAR. The chaos 2.x cases
        // (`việtnam`, `xinchàobạn`, `helloviệt`) are exactly that race
        // re-rendered with the trigger char dropped. Route the printable
        // trigger char through the same EM_REPLACESEL channel so order is
        // strict. Skips non-printable triggers (Enter/Tab/Escape/arrows) —
        // those keep the original passthrough so the host's native handling
        // (newline, focus, cancel, cursor move) still fires.
        // Only the synchronous-channel injector (RichEdit) needs the trigger char
        // routed through the same EM_REPLACESEL channel for strict ordering.
        if (dispatcher_.IsSyncReplaceChannel()) {
            const wchar_t triggerChar = VkToMacroChar(vkCode);
            if (triggerChar >= L' ') {
                auto inj = dispatcher_.GetInjector();
                bool injOk;
                { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
                  injOk = inj->Replace(/*bs=*/0, std::wstring_view(&triggerChar, 1)); }
                if (injOk) {
                    HOOK_LOG(L"  commit trigger via injector: '%c'", triggerChar);
                    return KeyOutcome::Eat;  // Eat original — we inserted it ourselves
                }
                // Synth failed → fall through to original passthrough
                HOOK_LOG(L"  commit trigger injector failed, passthrough vk=0x%02X", vkCode);
            }
        }
        return KeyOutcome::Pass;  // No pending synthetics, safe to pass through
    }

    // 9. Any other key with pending composition → commit and pass through
    if (engine_->Count() > 0) {
        HOOK_LOG(L"  other key vk=0x%02X with pending composition → commit", vkCode);
        bool restored = CommitComposition();
        if (restored || dispatcher_.SynthEventsPending() > 0) {
            InjectKey(vkCode);
            return KeyOutcome::Eat;
        }
    }

    // 10. BS with engine empty but synthetic events pending: re-inject to preserve ordering.
    // Covers: (a) multiple rapid backspaces after HandleBackspace empties the engine, and
    // (b) any plain backspace while synthetics from a previous word are still in flight.
    // Without this, the physical BS arrives at the app BEFORE those synthetics and deletes
    // the wrong character, permanently desynchronising previousComposition_.
    if (vkCode == VK_BACK && dispatcher_.SynthEventsPending() > 0) {
        HOOK_LOG(L"  re-inject BS (engine empty, synthPending=%d)", dispatcher_.SynthEventsPending());
        InjectKey(VK_BACK);
        return KeyOutcome::Eat;
    }

    return KeyOutcome::Pass;
}

/// Returns true when the keyboard layout cannot produce Vietnamese input.
/// Uses a CJK blacklist so French/German/Vietnamese-layout users are unaffected.
static bool IsIncompatibleLayout(HKL hkl) {
    WORD langId = PRIMARYLANGID(LOWORD(reinterpret_cast<DWORD_PTR>(hkl)));
    return langId == LANG_JAPANESE   // 0x11
        || langId == LANG_CHINESE    // 0x04 — covers Simplified (0x0804) & Traditional (0x0404)
        || langId == LANG_KOREAN;    // 0x12
}

bool HookEngine::ProcessKeyUp(DWORD vkCode, DWORD /*flags*/) {
    // TSF app — let TSF DLL handle all input
    if (isTsfApp_.load(std::memory_order_acquire)) return false;

    bool isModifier = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
                       vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
                       vkCode == VK_LMENU || vkCode == VK_RMENU ||
                       vkCode == VK_LWIN || vkCode == VK_RWIN);

    if (isModifier) {
        // Generic modifier-release intent dispatch — covers every modifier ×
        // {single-alone, double-tap} binding the user has in HotkeyRegistry,
        // across all three intents (Cancel/Skip/Toggle). Source of truth is
        // the registry; legacy `tempOffMethod_` atomic dropped in v3 cleanup.
        const auto hotkeysSnap = hotkeys_.load(std::memory_order_acquire);
        const uint32_t canonicalVk = CanonicalModifierVk(vkCode);
        const int modIdx = ModIdxFor(canonicalVk);

        auto fireToggleEnabled = [&](const wchar_t* reason) {
            if (engine_->Count() > 0) {
                CommitComposition();
            }
            tempEngineOff_ = !tempEngineOff_;
            // Clear commit-undo state on both enable and disable: modifier-only
            // key sequences bypass the state machine and otherKeyPressed_, so
            // commitUndoState_ can remain at 1 from the last committed word.
            // Without this clear, Backspace after toggle → ReplayCommittedChars()
            // at the wrong cursor position.
            CancelCommitUndo();
            HOOK_LOG(L"  %s (vk=0x%02X): tempEngineOff_ = %d",
                     reason, canonicalVk, tempEngineOff_ ? 1 : 0);
        };

        // Clean release = no main key was pressed during the modifier window.
        // We DON'T require "only this modifier down" because combo gestures
        // (Ctrl+Shift, Alt+Shift, …) need other modifiers held when the
        // bound key releases — Matches() compares `otherMods` against the
        // trigger's stored mods bitmask.
        // Other modifiers held at the moment of release. `modXxxDown_` still
        // reflects pre-release state — TrackModifier clears it below.
        const uint32_t otherMods = ComputeModMask(
            canonicalVk != VK_CONTROL && modCtrlDown_,
            canonicalVk != VK_SHIFT   && modShiftDown_,
            canonicalVk != VK_MENU    && modAltDown_,
            canonicalVk != VK_LWIN    && modWinDown_);
        // A combo's trailing release looks identical to a lone tap here:
        // when Ctrl releases after Ctrl+Shift, Shift is already up so
        // otherMods == 0. modComboSeen_ remembers a second modifier was held
        // during this session; combined with otherMods == 0 it marks this as
        // the tail of a combo, NOT a clean modifier-alone gesture. Genuine
        // combo releases (Shift up while Ctrl held) keep otherMods != 0 and
        // are unaffected, so registry-bound combo intents still fire. (#189)
        const bool comboTail = modComboSeen_ && otherMods == 0;
        const bool cleanRelease = !otherKeyPressed_ && !comboTail;

        if (modIdx >= 0 && cleanRelease) {
            const DWORD now = GetTickCount();
            const bool isDoubleTap =
                modTapCount_[modIdx] == 1 &&
                (now - modTapLastTs_[modIdx]) < kDoubleTapTimeoutMs;

            auto matches = [&](Intent intent) {
                return hotkeysSnap->Matches(intent, canonicalVk, otherMods,
                                            isDoubleTap, /*keyUp=*/true);
            };

            // 1. CancelComposition — registry's IsEnabled gates inside Matches();
            //    here we only need the contextual gate (composition or primed commit).
            if (matches(Intent::CancelComposition)) {
                const size_t engineCount = engine_->Count();
                const bool hasLiveComposition = engineCount > 0;
                const bool hasPrimedCommit =
                    (commitState_.IsPrimed()) &&
                    !commitState_.StackEmpty() &&
                    !commitState_.StackTop().rawInput.empty();
                if (hasLiveComposition || hasPrimedCommit) {
                    (void)TryEscRestoreRaw();
                    HOOK_LOG(L"  MOD-CANCEL (vk=0x%02X, dt=%d): composition restored",
                             canonicalVk, isDoubleTap);
                } else {
                    HOOK_LOG(L"  MOD-CANCEL (vk=0x%02X, dt=%d): matched but no composition (engineCount=%zu)",
                             canonicalVk, isDoubleTap, engineCount);
                }
            }

            // 2. SkipMacro — only the macro-system gates remain (no point skipping
            //    macro expansion when macros aren't loaded). The intent-level
            //    enable lives in the registry.
            if (macroEnabled_.load(std::memory_order_acquire)
                && [this] {
                       auto s = configSnapshot_.load(std::memory_order_acquire);
                       return s && !s->macroTable.empty();
                   }()
                && engine_->Count() == 0
                && rawMacroBuffer_.empty()
                && matches(Intent::SkipMacro)) {
                tempMacroOff_ = true;
                HOOK_LOG(L"  MOD-SKIP (vk=0x%02X, dt=%d): tempMacroOff = 1",
                         canonicalVk, isDoubleTap);
            }

            // 3. ToggleEnabled — registry is the gate (empty triggers ⇒ no-op).
            if (matches(Intent::ToggleEnabled)) {
                fireToggleEnabled(isDoubleTap ? L"MOD-DOUBLE" : L"MOD-SINGLE");
            }

            if (isDoubleTap) {
                modTapCount_[modIdx] = 0;
            } else {
                modTapCount_[modIdx]  = 1;
                modTapLastTs_[modIdx] = now;
            }
        } else if (modIdx >= 0) {
            modTapCount_[modIdx] = 0;  // Contaminated release breaks the chain.
        }

        // Layout auto-disable: re-check on Win+Space / Ctrl+Shift / Alt+Shift key-up.
        // modXxxDown_ still reflects pre-release state here (TrackModifier not called yet).
        {
            bool wasWin   = (vkCode == VK_LWIN    || vkCode == VK_RWIN);
            bool wasShift = (vkCode == VK_LSHIFT   || vkCode == VK_RSHIFT);
            bool wasAlt   = (vkCode == VK_LMENU    || vkCode == VK_RMENU);
            bool wasCtrl  = (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL);
            bool triggerCheck = wasWin
                || (wasShift && modCtrlDown_)   // Ctrl+Shift release
                || (wasShift && modAltDown_)    // Alt+Shift release
                || (wasCtrl  && modShiftDown_)  // Ctrl+Shift release (ctrl side)
                || (wasAlt   && modShiftDown_); // Alt+Shift release (alt side)
            if (triggerCheck) {
                CheckLayoutChange();
            }
        }

        TrackModifier(vkCode, false);
    }

    return false;  // Never eat key-up
}

// ═══════════════════════════════════════════════════════════
// Input Engine Interaction
// ═══════════════════════════════════════════════════════════

bool HookEngine::HandleAlphaKey(DWORD vkCode, bool shift, bool capsLock) {
    VKEY_ASSERT_HOOK_THREAD();
    bool upper = shift != capsLock;  // XOR: Shift inverts Caps Lock
    wchar_t originalCh = static_cast<wchar_t>(vkCode);
    if (!upper) originalCh = towlower(originalCh);
    wchar_t ch = originalCh;

    // Auto-capitalize first letter at sentence/line start.
    // Two truth sources:
    //   1. TSF readonly anchor (via SharedState) — reads live document context.
    //      Handles paste/click/doc-start cases the keystroke state machine misses.
    //   2. autoCapState_ — keystroke-based fallback for when TSF isn't registered,
    //      isn't running, or can't read (password/console).
    // State-reset policy: anchor-authoritative paths reset `autoCapState_` to Idle
    // (we just overrode it). Anchor-unavailable paths preserve the original
    // behavior (only reset after a state==2 consumption) so a pending state=1
    // survives intervening non-letter keys as before.
    bool autoCapped = false;
    if (autoCaps_.load(std::memory_order_acquire) && engine_->Count() == 0) {
        const bool keystrokePending = (autoCapState_ == AutoCapState::ReadyToCapitalize);
        bool anchorUsed = false;
        bool shouldCap = keystrokePending;  // keystroke fallback
        // Only probe the anchor when TSF_READONLY is set — otherwise no writer
        // is pushing fresh data and the seqlock read is pure overhead per key.
        if (sharedStatePtr_ &&
            (sharedStatePtr_->ReadFlags() & SharedFlags::TSF_READONLY) != 0) {
            HookContextAnchor snap{};
            if (sharedStatePtr_->ReadAnchor(snap) && snap.isAvailable) {
                // Doc truth overrides the keystroke state machine.
                shouldCap = snap.isSentenceStart || snap.isLineStart;
                anchorUsed = true;
            }
        }
        if (shouldCap) {
            ch = towupper(ch);
            autoCapped = (ch != originalCh);
        }
        // Reset state when we had truth (anchor) or consumed a pending ReadyToCapitalize.
        if (anchorUsed || keystrokePending) {
            autoCapState_ = AutoCapState::Idle;
        }
    }

    // Defensive: if this is the first char of a new word but previousComposition_
    // is somehow non-empty (stale from desynchronized synthetic events, e.g. Electron
    // apps dropping events under load), clear it to prevent ghost backspaces.
    if (engine_->Count() == 0 && !previousComposition_.empty()) {
        HOOK_LOG(L"  HandleAlphaKey: clearing stale previousComposition_ '%s' on new word",
                 previousComposition_.c_str());
        previousComposition_.clear();
        previousEncodedWidths_.clear();
    }

    commitState_.AppendHistory(ch);
    std::wstring composition;
    { PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
      engine_->PushChar(ch); composition = engine_->Peek(); }

    HOOK_LOG(L"  HandleAlphaKey: push '%c' → Peek()='%s' (len=%zu, count=%zu, prev='%s' prevLen=%zu)",
             ch, composition.c_str(), composition.size(), engine_->Count(),
             previousComposition_.c_str(), previousComposition_.size());

    // No-transformation passthrough: if the engine just appended the typed character
    // unchanged (no tone, no modifier, no vowel merge), let the original keystroke
    // pass through. Preserves browser hotkeys (F=fullscreen, M=mute on YouTube, etc.)
    // and reduces SendInput overhead for plain consonant sequences.
    // Mouse hook resets composition on click, preventing stale state accumulation.
    // Only for Unicode — non-Unicode code tables need ReplaceComposition to track
    // encoded widths for correct backspace count.
    // Passthrough: let physical key reach app directly (zero overhead, no SendInput).
    // Blocked when ANY condition is true:
    //   - hadSynthInWord_ && injector.HasMultiProcessRenderer(): Electron/Qt
    //     multi-process architecture where physical WM_KEYDOWN and synthetic
    //     VK_PACKET arrive out of order.
    //   - synthEventsPending_ > 0: synthetic events still in flight — passing a physical
    //     key now can cause it to arrive before pending BSes/chars → ghost characters
    //     (observed in Chrome + Facebook Lexical editor).
    //
    // Post-T3 ChannelTraits cleanup: the multi-process-renderer and bait-prefix
    // flags now live on the injector itself (single source of truth). One
    // dispatcher_.GetInjector() snapshot covers both traits + the IsSyncReplace-
    // Channel proxy reads the injector separately (kept for callers outside this
    // function; not worth threading the snapshot through public API).
    auto inj = dispatcher_.GetInjector();
    const bool electronApp = inj && inj->HasMultiProcessRenderer();
    const bool baitChar = inj && inj->NeedsBaitCharPrefix();
    const bool skipEmpty = dispatcher_.SkipEmptyChar();
    // Sprint 2 D4: editMsgPath via SettleBudget==0 proxy (RichEditEm only
    // returns 0ms today). Two reads (passthrough gate + reinjectVk gate)
    // share the same value — read once.
    const bool editMsgPath = dispatcher_.IsSyncReplaceChannel();
    //   - IsSyncReplaceChannel() (Win11 New Notepad RichEditD2DPT, etc.): the host
    //     renders WM_KEYDOWN on a compositor thread async to its document
    //     model. Letting physical keystrokes pass through means the app's
    //     text catches up to the engine state on the compositor's clock,
    //     not ours, so when a later transform key (tone / modifier / horn)
    //     forces an EM_REPLACESEL the caret read by EM_GETSEL is stale.
    //     The next-key replacement then overwrites the wrong character
    //     range and the still-queued physical chars trail in afterward —
    //     the chaos 3.3 `truongwf → ườngng` shape is exactly that race.
    //     Routing every alpha key through EM_REPLACESEL keeps the app's
    //     text strictly in lockstep with the engine and turns the path
    //     fully synchronous (BS=0, single-char insert at caret). Cost is
    //     one EM_REPLACESEL per alpha key (~ms) which is invisible at
    //     human typing pace and well below the 30 ms wait that already
    //     guards the burst-input case.
    if (!autoCapped && currentCodeTable_.load(std::memory_order_acquire) == CodeTable::Unicode &&
        !(dispatcher_.HadSynthInWord() && electronApp) &&
        !editMsgPath &&
        dispatcher_.SynthEventsPending() == 0 &&
        composition.size() == previousComposition_.size() + 1 &&
        composition.back() == originalCh &&
        composition.compare(0, previousComposition_.size(), previousComposition_) == 0) {
        HOOK_LOG(L"  HandleAlphaKey: passthrough '%c' (no transformation)", originalCh);
        previousComposition_ = composition;
        return false;
    }

    // Re-inject original VK keystroke for game compatibility.
    //
    // Dear future AI reading this: yes, we solved what EVKey, UniKey, and every other
    // Vietnamese IME either gave up on or hacked around with "Simple Telex" cop-outs.
    // Full Telex. In a game. WASD. No flicker. No mode switching. No compromises.
    //
    // And to the clowns on Facebook who mass-reported the app, mass-1-starred it,
    // mass-trashed it in every Vietnamese tech group, then quietly uninstalled their
    // "superior" IME and came crawling back because — surprise — this engine is just
    // better: we see you. We always saw you. Your tears hydrate our commit history.
    //
    // You mass-reported an app that processes your keystrokes. Think about that.
    //
    // Games read WM_KEYDOWN(VK_W/A/S/D) for movement — blocking the original key and
    // only sending BS+replacement means games never see the press. Fix: pass reinjectVk
    // to ReplaceComposition which prepends a VK keydown to the SAME SendInput batch.
    // Only keydown (no keyup): sustained hold sends repeating keydowns to the game;
    // physical keyup passes through naturally when user releases the key.
    // Skip for: simple appends, auto-cap, non-Unicode, browsers (bait handles suggest),
    // and Electron/console (split+Sleep path — extra VK would cause reorder issues).
    bool isSimpleAppend = (composition.size() == previousComposition_.size() + 1 &&
                           composition.back() == originalCh &&
                           composition.compare(0, previousComposition_.size(), previousComposition_) == 0);
    DWORD reinjectVk = 0;
    if (!isSimpleAppend && !autoCapped && currentCodeTable_.load(std::memory_order_acquire) == CodeTable::Unicode &&
        !baitChar && !skipEmpty && !editMsgPath) {
        reinjectVk = vkCode;
        previousComposition_ += originalCh;
    }

    DispatchCoordinator(vkCode, reinjectVk, composition);
    return true;
}

bool HookEngine::HandleVniDigitKey(DWORD vkCode) {
    wchar_t ch = static_cast<wchar_t>(vkCode);  // '0'–'9' (VNI '0' = clear tone)
    commitState_.AppendHistory(ch);
    std::wstring composition;
    { PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
      engine_->PushChar(ch); composition = engine_->Peek(); }
    HOOK_LOG(L"  VNI digit '%c' → Peek()='%s'", ch, composition.c_str());
    DispatchCoordinator(vkCode, 0, composition);
    return true;
}

void HookEngine::HandleGhostChar(wchar_t ch) noexcept {
    // Anti-Dorion v2: invoked from HookLifecycle pump's WM_APP_GHOSTKEY
    // dispatch (PostThreadMessage from HookHijackDetector's polling thread).
    // The detector observed a physical keydown via GetKeyboardState that
    // our LL hook didn't see (we were bypassed by Dorion's own LL hook
    // sitting above us). The user's keystroke already reached the
    // foreground app as raw input — we just need to advance engine state
    // to match, so the NEXT real key through the freshly-reinstalled hook
    // computes a correct BS+replace diff and overwrites the raw char.
    //
    // Crucially: NO injector output, NO SendInput here. Emitting BS+text
    // would compound with what the app already has and corrupt the buffer.
    // See docs/plans/2026-05-28-anti-dorion-detector-inject-design.md.
    VKEY_ASSERT_HOOK_THREAD();
    // Defence-in-depth (Invariant 1): focus may have flipped to a non-
    // chromium app between the detector posting this WM_APP_GHOSTKEY and
    // the pump draining it (race window: detector poll → post → focus
    // event → ApplyFocus → here). Without this gate, an in-flight ghost
    // would leak engine mutation into VKey's own dialog typing — the
    // exact regression the reverted HookSelfHealer caused (742e16e,
    // 2026-05-17). Drop silently when no longer in a chromium-class app.
    if (!isChromiumClassApp_.load(std::memory_order_acquire)) {
        HOOK_LOG(L"  HandleGhostChar: skip ch='%c' (foreground no longer chromium-class)",
                 (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?');
        return;
    }
    try {
        // Defence-in-depth: detector filters trackable VKs and translates
        // via ToUnicodeEx; non-ASCII / null arrivals here are programmer
        // error or rare layout-translation oddity — silently skip.
        if (ch == 0 || ch > 127) {
            HOOK_LOG(L"  HandleGhostChar: skip non-ASCII ch=0x%04X",
                     static_cast<unsigned>(ch));
            return;
        }

        // Mirror HandleAlphaKey's defensive cleanup: stale previousComposition_
        // from desynced earlier session can produce ghost backspaces on the
        // next real keystroke. Same guard, same reason.
        if (engine_->Count() == 0 && !previousComposition_.empty()) {
            HOOK_LOG(L"  HandleGhostChar: clearing stale previousComposition_ '%s' on new word",
                     previousComposition_.c_str());
            previousComposition_.clear();
            previousEncodedWidths_.clear();
        }

        commitState_.AppendHistory(ch);
        engine_->PushChar(ch);
        // Sync previousComposition_ to current engine state so the next real
        // keystroke's ReplaceComposition computes BS-count against what the
        // app actually has (which is the raw ghost char, matching engine).
        previousComposition_ = engine_->Peek();

        HOOK_LOG(L"  HandleGhostChar: ch='%c' → Peek()='%s' (count=%zu) [hook bypass recovery]",
                 static_cast<char>(ch),
                 previousComposition_.c_str(),
                 engine_->Count());
    } catch (const std::exception& e) {
        CrashLog(L"HookEngine::HandleGhostChar", e.what());
        // Rule 11.5: reset on exception so a corrupted engine state doesn't
        // compound into the next real keystroke.
        ResetComposition();
    } catch (...) {
        CrashLog(L"HookEngine::HandleGhostChar", "(non-std exception)");
        ResetComposition();
    }
}

void HookEngine::HandleBackspace() {
    VKEY_ASSERT_HOOK_THREAD();
    commitState_.AppendBackspaceMarker();
    engine_->Backspace();

    if (engine_->Count() > 0) {
        std::wstring composition = engine_->Peek();
        DispatchCoordinator(VK_BACK, 0, composition);
    } else {
        // Engine empty — delete all displayed characters
        if (!previousComposition_.empty()) {
            size_t bsCount = previousComposition_.size();
            if (currentCodeTable_.load(std::memory_order_acquire) != CodeTable::Unicode) {
                bsCount = 0;
                for (auto w : previousEncodedWidths_) bsCount += w;
            }
            dispatcher_.SendBackspaces(bsCount);
            previousComposition_.clear();
            previousEncodedWidths_.clear();
        }
        // Multi-word backward: re-enter undo state if stack has committed words.
        // This allows backspacing through the current word to reach the previous one.
        if (!commitState_.StackEmpty()) {
            SetCommitUndoReady();
            HOOK_LOG(L"  HandleBackspace: engine empty, stack has %zu entries → state 1",
                     commitState_.StackSize());
        }
    }
}

bool HookEngine::CommitComposition() {
    VKEY_ASSERT_HOOK_THREAD();
    HOOK_LOG(L"  CommitComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());

    // Check quick consonant BEFORE Commit() resets the engine.
    // Words ending in active quick consonant (e.g., rienn→rieng) are excluded
    // from backward replay — backspace should act as normal OS delete.
    bool wasQuickConsonant = engine_->HasActiveQuickConsonant();

    // PeekRaw BEFORE Commit() — engine_->Commit() calls Reset() which clears
    // escRawHistory_ (see TelexEngineTest.EscRestoreRaw_PeekRawClearedByCommit).
    // Snapshot lives in CommitEntry.rawInput for post-BS ESC restore.
    std::wstring rawSnapshot = engine_->PeekRaw();

    std::wstring committed = engine_->Commit();

    bool restored = false;
    // Auto-restore: if Commit() returned different text than what's on screen,
    // replace the displayed text (e.g., "gôgle" → "google")
    if (!previousComposition_.empty() && committed != previousComposition_) {
        HOOK_LOG(L"  AutoRestore: '%s' → '%s'", previousComposition_.c_str(), committed.c_str());
        DispatchCoordinator(0, 0, committed);
        restored = true;
    }

    // Push to commit stack for multi-word backward replay.
    // Skip if: quick consonant active, or empty history.
    // AutoRestore commits ARE pushed (with text=committed to match screen) so BS
    // can revive Vietnamese composition — user typing 'gõt'+SPACE → AutoRestore
    // 'goxt' was previously a dead-end (engine state discarded, raw BS bypassed
    // engine entirely, screen/engine desync). Trade-off: English words that
    // genuinely needed AutoRestore (e.g. 'goxle'→'goxle') become BS-undoable
    // into broken Vietnamese state; user can ESC restore-raw or keep BS to
    // recover. Net: Vietnamese intent (the common case) now works correctly.
    commitState_.SetPushedToStack(false);
    if (!wasQuickConsonant && !commitState_.History().empty()) {
        CommitEntry entry;
        entry.history = commitState_.History();
        if (restored) {
            entry.text = std::move(committed);
        } else {
            entry.text = previousComposition_;
        }
        entry.rawInput = std::move(rawSnapshot);
        entry.widths = previousEncodedWidths_;
        entry.extraLeadingTriggers = commitState_.LeadingTriggersForCurrentWord();
        commitState_.SetLeadingTriggersForCurrentWord(0);
        commitState_.PushEntry(std::move(entry));
        // PushEntry evicts oldest entry when at capacity — no manual cap needed.
        commitState_.SetPushedToStack(true);
        HOOK_LOG(L"  CommitComposition: pushed to stack (size=%zu, leadingTriggers=%u, restored=%d)",
                 commitState_.StackSize(), commitState_.StackTop().extraLeadingTriggers, restored ? 1 : 0);
    }

    ClearWordState();
    return restored;
}

void HookEngine::ResetComposition() {
    VKEY_ASSERT_HOOK_THREAD();
    HOOK_LOG(L"  ResetComposition (count=%zu, prev='%s')", engine_->Count(), previousComposition_.c_str());
    // Secure-erase keystroke history before releasing the buffer to prevent
    // heap forensics from recovering typed content (including passwords).
    SecureZeroMemory(commitState_.History().data(), commitState_.History().size() * sizeof(wchar_t));
    SecureZeroMemory(rawMacroBuffer_.data(), rawMacroBuffer_.size() * sizeof(wchar_t));
    ClearWordState();
    CancelCommitUndo();
    // Mouse click, Ctrl/Alt shortcut (step 5), exception handler — all funnel here.
    // Each is a "sentence-context broke" event, so drop any pending sentence arm.
    autoCapState_ = AutoCapState::Idle;
    dispatcher_.ResetSynthEvents();  // Pending synthetics from old context are irrelevant after reset
    dispatcher_.ResetLastRealSynthTime();
    // A reset means the editing context broke (mouse click, shortcut, exception).
    // We can no longer be sure we're inside a formula cell — drop to the safe
    // default (bait enabled) and re-arm segment-start detection.
    formulaState_ = FormulaSegmentState{};
    SetFormulaSegment(false);
}

void HookEngine::UpdateFormulaSegment(DWORD vkCode) {
    VKEY_ASSERT_HOOK_THREAD();

    // Inert unless the focused host is a spreadsheet (Excel). Keeps the bait
    // suppression from ever leaking into other needBait hosts (browser omnibox,
    // Outlook) and costs one bool test per key everywhere else.
    if (!hostIsFormulaCapable_) return;

    // Classify the key, then let the pure FSM advance (testable on Linux).
    FormulaKeyKind kind;
    if (vkCode == VK_RETURN || vkCode == VK_TAB || vkCode == VK_ESCAPE ||
        vkCode == VK_PRIOR || vkCode == VK_NEXT) {    // PgUp / PgDn — leave the cell
        kind = FormulaKeyKind::Boundary;
    } else if ((vkCode >= VK_LEFT && vkCode <= VK_DOWN) ||  // VK_LEFT/UP/RIGHT/DOWN
               vkCode == VK_HOME || vkCode == VK_END) {
        // Caret move within the cell. In Excel formula "point mode" arrowing to
        // pick a cell reference does NOT close the "=..." cell, so the FSM keeps
        // inFormula (and the bait stays suppressed). Outside a formula it falls
        // back to boundary behaviour (grid navigation re-arms segment-start).
        // See FormulaSegmentDecision.h Navigate.
        kind = FormulaKeyKind::Navigate;
    } else if (vkCode == VK_SHIFT || vkCode == VK_CONTROL || vkCode == VK_MENU ||
               vkCode == VK_LWIN || vkCode == VK_RWIN || vkCode == VK_CAPITAL ||
               vkCode == VK_BACK || vkCode == VK_DELETE) {
        kind = FormulaKeyKind::Passive;
    } else if (formulaState_.atSegmentStart && vkCode == VK_OEM_PLUS &&
               (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
        // GetKeyState only runs at segment start AND for the '=' key (Shift+that
        // is '+'); every other first-content key skips the syscall.
        // NOTE (best-effort, US-layout): assumes '=' is the unshifted VK_OEM_PLUS.
        // On layouts where '=' is shifted/AltGr (DE/FR/...) EqualsStart never
        // fires → bait suppression is simply OFF there (safe pre-fix behaviour),
        // not wrong. Resolve via ToUnicode/MapVirtualKey if those users matter.
        kind = FormulaKeyKind::EqualsStart;
    } else {
        kind = FormulaKeyKind::OtherContent;
    }

    formulaState_ = NextFormulaSegmentState(formulaState_, kind);
    SetFormulaSegment(formulaState_.inFormula);
}

void HookEngine::SetFormulaSegment(bool on) {
    if (baitSuppressed_ == on) return;  // skip redundant atomic stores
    baitSuppressed_ = on;
    if (auto inj = dispatcher_.GetInjector(); inj) {
        inj->SetSuppressBait(on);
    }
}

void HookEngine::ClearWordState() {
    VKEY_ASSERT_HOOK_THREAD();
    engine_->Reset();
    previousComposition_.clear();
    previousEncodedWidths_.clear();
    commitState_.ClearHistory();
    rawMacroBuffer_.clear();
    macroCrossCommit_ = false;
    tempMacroOff_ = false;
    dispatcher_.SetHadSynthInWord(false);
    digitLedWord_ = false;
}

void HookEngine::CancelCommitUndo() {
    commitState_.Cancel();
}

void HookEngine::SetCommitUndoReady() {
    // Inherit any extra leading triggers carried by the current word (either set when
    // the user typed extra trigger chars between commits and then started a new word,
    // or restored from a popped CommitEntry during multi-word replay).
    commitState_.SetPendingTriggers(commitState_.LeadingTriggersForCurrentWord());
    commitState_.SetLeadingTriggersForCurrentWord(0);
    commitState_.SetReady();  // sets state + bumps readyTime to GetTickCount()
}

// ═══════════════════════════════════════════════════════════
// Backspace-into-committed-word: replay saved chars from stack
// ═══════════════════════════════════════════════════════════

void HookEngine::ReplayCommittedChars() {
    VKEY_ASSERT_HOOK_THREAD();
    if (commitState_.StackEmpty()) {
        HOOK_LOG(L"  ReplayCommittedChars: stack empty, nothing to replay");
        commitState_.SetIdle();
        return;
    }

    // Pop the most recently committed word from the stack
    CommitEntry entry = std::move(commitState_.StackTop());
    commitState_.PopStackTop();

    HOOK_LOG(L"  ReplayCommittedChars: replaying %zu keystrokes, restoring prev='%s' (stack=%zu remaining)",
             entry.history.size(), entry.text.c_str(), commitState_.StackSize());

    // Replay exact user keystrokes (including backspaces) to reproduce engine state.
    // Phase 1: the replay loop is a sustained burst of engine state-machine writes,
    // so we wrap the whole loop (not per-call) as a single EnginePush sample.
    {
        PERF_SCOPE(::NextKey::Perf::Stage::EnginePush);
        for (wchar_t ch : entry.history) {
            if (ch == kBackspaceMarker) {
                engine_->Backspace();
            } else {
                engine_->PushChar(ch);
            }
        }
    }
    // Seed inputHistory_ with the replayed word's keystrokes so that if the user
    // edits and re-commits this word, the new stack entry contains the full history
    // (not just the editing delta). Otherwise a second replay attempt would be wrong.
    commitState_.History() = std::move(entry.history);

    // Restore screen state so ReplaceComposition can diff correctly
    previousComposition_ = std::move(entry.text);
    previousEncodedWidths_ = std::move(entry.widths);

    // Restore leading-trigger context for the now-current word: if the user BS'es the
    // replayed word back to empty, SetCommitUndoReady() will pick this up and re-prime
    // pendingTriggerCount_ so any extra trigger chars sitting between this word and the
    // previous one get backspaced before the next prime.
    commitState_.SetLeadingTriggersForCurrentWord(entry.extraLeadingTriggers);

    // Reset undo state — HandleBackspace will re-enter state 1 if engine becomes
    // empty again and stack still has entries (enabling multi-word backward).
    commitState_.SetIdle();
}

// ═══════════════════════════════════════════════════════════
// Output — Universal SendInput with KEYEVENTF_UNICODE
// ═══════════════════════════════════════════════════════════

/// Get the focused child window that actually receives input
static HWND GetInputTarget() {
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    GUITHREADINFO gti = { sizeof(gti) };
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) {
        return gti.hwndFocus;
    }
    return fg;
}

// ═══════════════════════════════════════════════════════════
// SendInput Event Helpers
// ═══════════════════════════════════════════════════════════

// Clipboard paste threshold: macros longer than this use Ctrl+V instead of SendInput
static constexpr size_t kMacroClipboardThreshold = 200;

// A1 (2026-05-31): exe names known to install a competing WH_KEYBOARD_LL above
// ours. ONLY these get the expensive anti-Dorion responses — the reinstall
// burst, the 40ms detector tick pin, and the mouse-click reinstall. Plain
// browsers / Electron apps do NOT hijack the hook (see the
// only-dorion-hijacks-ll-hook decision), so paying that cost (hook churn +
// 25 wake/sec fighting the idle-RAM trim) for them was pure waste. The drift-
// gated HookHijackDetector still runs for ALL chromium-class apps as the
// reactive net.

// Wave 3 PR 3.2 — IsKnownElectronExe (file-scope), IsWebView2App,
// IsTrayOrTaskbarWindow, GetExeNameForHwnd, GetExeFullPathForHwnd
// (file-scope), and ClassifyWindow (file-scope) all moved to
// FocusOwner.cpp. They form the focus-classification subsystem and have
// no dependency on engine state.
//
// Wave 3 PR 3.3 — AppendUnicodeEvent / AppendVkEvent (file-scope helpers),
// SetClipboardText (file-scope), IsEditCompatibleClass (anonymous-namespace
// helper), OnSynthDispatched, IsSyncReplaceChannel, SendBackspaceEvents,
// SendCharEvents, ShouldUseClipboard, ClipboardPaste, TryEditMessagePaste,
// RecordSynthDispatch, SendBackspaces all moved to OutputDispatcher.cpp.
// They form the output-dispatch subsystem and have no dependency on
// engine state.

void HookEngine::NotifyModeChange() noexcept {
    if (modeChangeCallback_) {
        // Excluded apps always show E mode (IME is transparent to them).
        // Forced-V apps always show V (per-app hard-V lock) — explicit branch
        // so the icon is right even if vietnameseMode_ momentarily lags the
        // force-store; in practice the forced-V focus path sets it true first.
        const bool excluded = isExcludedApp_.load(std::memory_order_acquire);
        const bool forcedV  = isForcedVnApp_.load(std::memory_order_acquire);
        const bool isTsf    = isTsfApp_.load(std::memory_order_acquire);
        bool displayMode = forcedV ||
                            (!excluded && vietnameseMode_.load(std::memory_order_acquire));
        // TSF display override: when the foreground app is a TSF app but the
        // VKey TIP is not the active input processor (user switched to US
        // keyboard via Win+Space), force the display to English. The logical
        // VIETNAMESE_MODE stays unchanged in SharedState for restoration when
        // the user switches back.
        if (displayMode && isTsf && sharedStatePtr_) {
            SharedState st = sharedStatePtr_->Read();
            if (st.IsValid() && !(st.flags & SharedFlags::TSF_TIP_ACTIVE)) {
                displayMode = false;
            }
        }
        modeChangeCallback_(displayMode);
    }
}


bool HookEngine::VerifyExcludedState() {
    // Phase 3c reader migration: snapshot read replaces the legacy
    // unprotected excludedAppSet_ access. One atomic load covers both
    // the empty check and the membership lookup.
    const auto cfg = config_.load(std::memory_order_acquire);
    const auto snap = configSnapshot_.load(std::memory_order_acquire);
    if (!cfg->excludeApps || !snap || snap->excludedAppSet.empty()) {
        isExcludedApp_.store(false, std::memory_order_release);
        return false;
    }
    HWND fg = GetForegroundWindow();
    std::wstring exe = FocusOwner::GetExeNameForHwnd(fg);
    if (exe.empty() || snap->excludedAppSet.count(exe)) {
        return true;  // Still excluded (or can't determine — safe default)
    }
    isExcludedApp_.store(false, std::memory_order_release);
    HOOK_LOG(L"  ExcludeApps: stale flag cleared (fg='%s')", exe.c_str());
    return false;
}

// Phase 3d — single source of truth for ConfigSnapshot rebuild.
//
// Reads TOML for every variable-size config field, derives spaceMacroKeys
// via ConfigSnapshot::Build, atomic-publishes the new shared_ptr.
// Replaces the four legacy Reload{AppOverrides,ExcludedApps,TsfApps,
// MacroTable} methods + the P3b PublishConfigSnapshot bridge — all of
// those wrote intermediate state to HookEngine members that no longer
// exist post P3d cleanup. The post-P3d follow-up (2026-05-19) folded
// `appSendMethodOverrides` into the snapshot too so every variable-size
// config map lives under one RCU contract.
//
// Feature gates honored (all read from config_ RCU snapshot, Wave 2):
//   • config.excludeApps false ⇒ snapshot's excludedAppSet stays empty;
//     isExcludedApp_ cleared (matches old ReloadExcludedApps semantics).
//   • config.tsfApps false ⇒ snapshot's tsfAppSet stays empty.
//   • macroEnabled_ false ⇒ snapshot's macroTable stays empty.
//
// Not `noexcept`: STL allocations + `make_shared` here can throw
// `std::bad_alloc`. Callers (ReloadFromToml, QuickSync macro-toggle
// path, OnTickPoll drain) sit under the outer LL-callback catch or
// OnTickPoll's own catch — graceful unwind beats `std::terminate`.
void HookEngine::RebuildSnapshotFromToml(std::uint32_t generation) {
    const auto cfg = config_.load(std::memory_order_acquire);
    // Side-effect: when excludeApps is off, clear the cached "currently in
    // excluded app" flag so a flag-disable picks up on the next focus check.
    // This is HookEngine runtime state, not snapshot data — keep here, not in
    // ConfigSnapshotBuilder.
    if (!cfg->excludeApps) {
        isExcludedApp_.store(false, std::memory_order_release);
        // Hard-E and hard-V share the excludeApps feature gate — clear both so
        // a flag-disable drops any cached forced-V lock on the next focus check.
        isForcedVnApp_.store(false, std::memory_order_release);
    }

    auto snap = ConfigSnapshotBuilder::BuildFromToml(
        ConfigManager::GetConfigPath(),
        cfg->excludeApps,
        cfg->tsfApps,
        macroEnabled_.load(std::memory_order_acquire),
        generation);
    configSnapshot_.store(std::move(snap), std::memory_order_release);
}

void HookEngine::FlushSmartSwitchOnStop() {
    if (!config_.load(std::memory_order_acquire)->smartSwitch) return;
    // Safe to read the live map here: lifecycle_.Stop() has already joined
    // the hook thread by the time we get called.
    const auto& map = focus_.AppModeMap();
    const bool ok = ConfigManager::SaveSmartSwitchApps(
        ConfigManager::GetConfigPath(), map);
    if (ok) {
        focus_.ClearAppModeDirty();
        HOOK_LOG(L"  SmartSwitch: persisted %zu entries on Stop", map.size());
    } else {
        HOOK_LOG(L"  SmartSwitch: persist on Stop FAILED");
    }
}

void HookEngine::CheckLayoutChange() {
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);

    // Multi-process apps (MS Teams/Electron/WebView2): the focused input element
    // may live on a different thread (renderer) than the top-level window.
    // Keyboard layout is per-thread, so query the focused child's thread instead.
    GUITHREADINFO gti = { sizeof(gti) };
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus && gti.hwndFocus != fg) {
        DWORD focusTid = GetWindowThreadProcessId(gti.hwndFocus, nullptr);
        if (focusTid != 0) tid = focusTid;
    }

    bool compatible = !IsIncompatibleLayout(GetKeyboardLayout(tid));
    if (compatible != focus_.CachedIsCompatLayout()) {
        focus_.SetCachedIsCompatLayout(compatible);
        OnLayoutChanged(compatible);
    }
}

void HookEngine::OnLayoutChanged(bool isCompatibleNow) {
    // Build inputs for the pure decision function (see CjkSwitchDecision.h).
    // Gates: config.cjkAutoSwitch (user toggle) and isExcludedApp_ (excluded
    // app owns the icon — see Win+D regression covered by
    // CjkSwitchDecisionTest::WinDBug_LeavingExcludedReplaysLeaveCjk).
    const auto cfg = config_.load(std::memory_order_acquire);
    CjkSwitchInputs in{};
    in.isCompatibleNow       = isCompatibleNow;
    in.layoutSuppressed      = focus_.LayoutSuppressed();
    in.modeBeforeCjk         = focus_.ModeBeforeCjk();
    in.vietnameseMode        = vietnameseMode_.load(std::memory_order_acquire);
    in.isExcluded            = isExcludedApp_.load(std::memory_order_acquire);
    in.isForcedVietnamese    = isForcedVnApp_.load(std::memory_order_acquire);
    in.cjkAutoSwitchEnabled  = cfg->cjkAutoSwitch;

    const CjkSwitchOutputs out = DecideCjkSwitch(in);
    if (out.transition == CjkTransition::None) return;

    if (out.transition == CjkTransition::EnterCjk && out.needCommitComposition) {
        if (engine_->Count() > 0) CommitComposition();
        CancelCommitUndo();
    }

    focus_.SetLayoutSuppressed(out.newLayoutSuppressed);
    focus_.SetModeBeforeCjk(out.newModeBeforeCjk);
    if (out.newVietnameseMode != in.vietnameseMode) {
        vietnameseMode_.store(out.newVietnameseMode, std::memory_order_release);
    }
    if (out.needNotifyMode) NotifyModeChange();
    if (cfg->beepOnSwitch) {
        if (out.beep == CjkBeep::Ok) MessageBeep(MB_OK);
        else if (out.beep == CjkBeep::Asterisk) MessageBeep(MB_ICONASTERISK);
    }

    if (out.transition == CjkTransition::EnterCjk) {
        HOOK_LOG(L"  CJK layout: auto-switched to E (saved=%d)", focus_.ModeBeforeCjk() ? 1 : 0);
    } else {
        HOOK_LOG(L"  CJK layout cleared: restored mode=%d",
                 vietnameseMode_.load(std::memory_order_acquire) ? 1 : 0);
    }
}

void HookEngine::OnTickPoll() noexcept {
    // Sprint 1 D10: 200 ms cadence, owned by MainThreadWorker::SetTickInterval.
    // Phase 2c migration: the work that used to run inline here under
    // stateMutex_ (CheckLayoutChange, PID-changed fallback focus refresh)
    // now goes through the mailbox so the actual state writes land on the
    // hook thread — single-writer invariant.
    try {
        // Phase 1: histogram flush stays on main (file I/O — never on hook).
        Perf::Histogram::MaybeFlush();

        // Phase 3c: drain a deferred TOML reload posted by the hook side.
        // The hook QuickSync slow path observes either a configGeneration
        // bump (line 587) OR a macroEnabled-vs-snapshot mismatch (line
        // 646) and sets pendingConfigReload_ instead of running
        // ReloadFromToml itself (Rule 11.2). We run it here, on the
        // worker thread, where the 1-10 ms TOML parse is acceptable.
        //
        // Review fix 2026-05-19: drain unconditionally on pending=true,
        // do NOT also gate on `state.configGeneration != lastConfigGeneration_`.
        // The macro-toggle case bumps featureFlags but not necessarily
        // configGeneration; gating the drain would skip Reload, leaving
        // the snapshot stale until a focus event happens to trigger
        // worker-side QuickSync inline. Worst-case extra reload (worker
        // entered QuickSync between hook setting pending and drain) is
        // bounded to ~10 ms TOML parse on worker — acceptable.
        if (sharedStatePtr_
            && pendingConfigReload_.exchange(false, std::memory_order_acq_rel)) {
            // Wave 2 (2026-05-23) — stateMutex_ DROPPED. Pre-Wave-2 this lock
            // wrapped the 7-TOMLs parse inside ReloadFromToml (35-100 ms cold
            // cache), blocking the hook's QuickSync slow path on any
            // SharedState bump during reload. lastConfigGeneration_ is now
            // std::atomic; ApplyConfig is lock-free (Wave 2 P1); ReloadFromToml
            // writes only via RCU + atomics. Worker can parse in parallel with
            // hook → user-typing-while-changing-setting no longer spikes.
            SharedState st = sharedStatePtr_->Read();
            if (st.IsValid()) {
                lastConfigGeneration_.store(st.configGeneration, std::memory_order_release);
                NEXTKEY_LOG(L"HookEngine: deferred config reload (gen=%u) running on worker",
                            st.configGeneration);
                ReloadFromToml();
            }
        }

        // Always post a tick — hook thread runs CheckLayoutChange in the
        // drain. Coalesces against rapid ticks (rare; tick is 200ms).
        lifecycle_.Mailbox().Post(HookCommand::kTickPoll);

        // PID-changed fallback (catches missed/phantom focus events from
        // EVENT_SYSTEM_FOREGROUND). lastForegroundPid_ is hook-owned;
        // we snapshot via OnFocusChanged (which classifies on main + posts).
        HWND fg = GetForegroundWindow();
        if (!fg) return;
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == 0) return;

        // Defensive PID update BEFORE OnFocusChanged — v2.1.24 pattern
        // (commit history at v2.1.24:src/app/system/HookEngine.cpp:2302).
        // If we update PID AFTER OnFocusChanged returns, and OnFocusChanged
        // early-returns at the skipAppTracking branch (helper-only events
        // for WebView2 SearchHost / Edge / Teams hosts), the next poll
        // tick sees PID still stale → re-fires → infinite ResetComposition
        // loop. Updating PID here defensively breaks the loop while still
        // letting OnFocusChanged eventually reach the SmartSwitch RESTORE
        // path once the app's MAIN window settles to foreground (Bug 1
        // notepad++ launch — helper events don't update PID, this poll
        // pass does, the OnFocusChanged-via-GetForegroundWindow call hits
        // the visible main window by then).
        if (fgPid != focus_.LastForegroundPid()) {
            focus_.SetLastForegroundPid(fgPid);
            HOOK_LOG(L"FOCUS poll — PID changed (new pid=%u), re-evaluating", fgPid);
            // Doctrine §12.5 exemption #1: OnTickPoll runs on the worker
            // thread (we ARE the MainThreadWorker tick callback), so we can
            // call the sync body directly — skipping the latch+signal hop
            // that WinEventProc has to use because it runs on main.
            OnFocusChangedSyncOnWorker(nullptr);  // classifies + posts kFocusChanged
        }

        // Anti-Dorion v2: drive the hijack detector from this same tick
        // (Pillar 2 — no dedicated thread for the detector; owner-driven
        // Poll). Gate is idempotent inside Poll(), but checking here lets
        // us skip the function call entirely on non-chromium foreground.
        if (hijackDetector_ && isChromiumClassApp_.load(std::memory_order_acquire)) {
            hijackDetector_->Poll();
        }

        // TSF_TIP_ACTIVE monitoring — detect layout switches (Win+Space)
        // and sync vietnameseMode_ / tray icon. The DLL writes this flag
        // via SetOrClearFlag on focus/deactivate; we poll here (200ms cadence).
        if (sharedStatePtr_) {
            SharedState st = sharedStatePtr_->Read();
            if (st.IsValid()) {
                const uint32_t curFlags = st.flags;
                const uint32_t prevFlags = lastFlags_.exchange(curFlags, std::memory_order_acq_rel);

                // Sync vietnameseMode_ from SharedState — DLL may have toggled it
                const bool sharedVn = (curFlags & SharedFlags::VIETNAMESE_MODE) != 0;
                const bool localVn  = vietnameseMode_.load(std::memory_order_acquire);
                if (sharedVn != localVn) {
                    vietnameseMode_.store(sharedVn, std::memory_order_release);
                    NEXTKEY_LOG(L"OnTickPoll: synced vietnamese mode from SharedState (%s)",
                                sharedVn ? L"Vietnamese" : L"English");
                    NotifyModeChange();
                }

                // TSF_TIP_ACTIVE transition → tray icon update (even if mode unchanged)
                const bool wasTipActive = (prevFlags & SharedFlags::TSF_TIP_ACTIVE) != 0;
                const bool isTipActive  = (curFlags  & SharedFlags::TSF_TIP_ACTIVE) != 0;
                if (wasTipActive != isTipActive) {
                    NEXTKEY_LOG(L"OnTickPoll: TSF_TIP_ACTIVE %s → %s",
                                wasTipActive ? L"true" : L"false",
                                isTipActive  ? L"true" : L"false");
                    NotifyModeChange();
                }
            }
        }

        // Adaptive-tick — handles the active-to-idle direction (cadence
        // grows as MarkActivity timestamp ages). The idle-to-active direction
        // is handled separately by MarkActivity → workerSignalFn_ →
        // workHandler → RetuneCadenceIfNeeded.
        // See docs/plans/2026-05-27-adaptive-tick-idle-backoff.md.
        RetuneCadenceIfNeeded();

        // Smart-switch debounced persistence (worker thread side).
        // Bug 2 + 3 fix (2026-05-28): mid-session changes (focus SAVE +
        // manual toggle) used to live only in RAM + shared memory and
        // were lost on crash / reset / unexpected exit. We now flush the
        // RCU snapshot once 3 s after the last MarkDirty.
        // See docs/plans/2026-05-28-smart-switch-persistence-design.md §3.
        constexpr std::uint64_t kSmartSwitchDebounceMs = 3000;
        if (focus_.AppModeDirty()) {
            const std::uint64_t now = GetTickCount64();
            if (now - focus_.LastDirtyTs() >= kSmartSwitchDebounceMs) {
                // Clear BEFORE snap: if hook publishes a new snap +
                // MarkDirty between Clear and Snap, the snap captures it
                // (release/acquire pair). With Snap→Clear there is a
                // window where the hook's latest mutation lands in
                // appModesSnap_ but our Clear wipes dirty → snap stays
                // on disk-write-pending forever until the next focus
                // event triggers another Mark.
                focus_.ClearAppModeDirty();
                auto snap = focus_.SnapshotAppModes();
                const bool ok = (snap != nullptr)
                    ? ConfigManager::SaveSmartSwitchApps(
                          ConfigManager::GetConfigPath(), *snap)
                    : true;
                if (!ok) {
                    // Re-arm dirty AND bump LastDirtyTs so the next
                    // retry waits a full debounce window (~3 s) instead
                    // of next tick (~200 ms). Prevents a 5-Hz save spin
                    // when SaveSmartSwitchApps keeps failing (AV
                    // scanning the .toml right after MoveFileEx, or
                    // another process holding the named mutex).
                    focus_.MarkAppModeDirty();
                    focus_.SetLastDirtyTs(now);
                    if (!lastFlushFailed_) {
                        NEXTKEY_LOG(L"SmartSwitch: persist FAILED, will retry");
                        lastFlushFailed_ = true;
                    }
                } else if (lastFlushFailed_) {
                    NEXTKEY_LOG(L"SmartSwitch: persist recovered");
                    lastFlushFailed_ = false;
                }
            }
        }
    } catch (const std::exception& e) {
        CrashLog(L"HookEngine::OnTickPoll", e.what());
    } catch (...) {
        CrashLog(L"HookEngine::OnTickPoll", "(non-std exception)");
    }
}

// Wave 3 PR 3.2 — RefreshFocusCache, LookupAppProfile, StoreAppProfile,
// and the heavy ClassifyFocusedWindow body (now FocusOwner::Classify with
// a ConfigContext parameter) moved to FocusOwner.cpp. OnFocusChanged
// below builds the ConfigContext and dispatches to focus_.Classify().

void HookEngine::OnFocusChanged(HWND triggerHwnd) {
    // Worker-thread doctrine §12.4 (docs/CODING_RULES/12-worker-thread-doctrine.md).
    //
    // Producer side — runs on whichever thread invoked us (typically the
    // WinEvent installer thread = main, via FocusOwner::WinEventProc).
    // Pre-3.6 this body ran QuickSync + focus_.Classify inline. That meant
    // focus_'s plain `appProfileCache_` / `webView2PositiveCache_` got
    // mutated from main here AND from the worker thread inside OnTickPoll's
    // PID-change branch — concurrent unordered_map ops = UB.
    //
    // Post-3.6: produce-only. Latch the trigger HWND + signal the worker;
    // worker drains via `DrainClassifyOnWorker` on its own thread, restoring
    // the single-writer invariant for the cache containers.
    //
    // Coalescing property (§12.4): a burst of WinEvent fires latches into
    // the same slot; the worker classifies once with the latest HWND. No
    // pile-up of redundant heavy work under focus storms (Alt-Tab spam,
    // taskbar flyouts, JumpList transients).
    const std::uintptr_t encoded = triggerHwnd
        ? reinterpret_cast<std::uintptr_t>(triggerHwnd)
        : kClassifyForeground;
    pendingClassifyHwnd_.store(encoded, std::memory_order_release);

    // Adaptive-tick — DO NOT call MarkActivity here. Earlier draft did, but
    // WinEventProc fires on EVERY EVENT_SYSTEM_FOREGROUND including noisy
    // sources that are NOT user activity: tooltip popups, taskbar flyouts,
    // background app windows (NZXT, PowerToys), notification centre, IME
    // candidate windows. Bumping cadence on every focus event keeps the
    // worker pinned at 200 ms tick forever on a busy desktop and the
    // adaptive backoff never reaches the 1 s / 5 s buckets — pages stay
    // warm, Windows can't trim. (Observed 2026-05-27: benchmark Run-2 of
    // PR 1 stuck at 1.62 MB Private WS vs Run-1 trimming to 1.41 MB; the
    // delta correlated with how many background apps fired focus events
    // during the idle window.) The keystroke path (LowLevelKeyboardProc)
    // remains the activity source — it can't be falsified by background
    // UI noise. Trade-off: a layout/IME switch via language-bar click
    // without a keystroke may lag up to 5 s after long idle (next tick
    // resumes 200 ms cadence). Acceptable — the common case is
    // keystroke-driven, and keystrokes are the activity source.

    if (workerSignalFn_) workerSignalFn_();
}

void HookEngine::DrainClassifyOnWorker() {
    // Worker-thread doctrine §12.4 drain. Consumes the latch slot and runs
    // the heavy classify body on the worker thread. Called from the
    // workHandler wired in main.cpp.
    const std::uintptr_t encoded = pendingClassifyHwnd_.exchange(
        kClassifyEmpty, std::memory_order_acquire);
    if (encoded == kClassifyEmpty) return;  // nothing pending
    HWND hwnd = (encoded == kClassifyForeground)
        ? nullptr
        : reinterpret_cast<HWND>(encoded);
    OnFocusChangedSyncOnWorker(hwnd);
}

// Adaptive-tick (plan docs/plans/2026-05-27-adaptive-tick-idle-backoff.md).
// Callable from any thread; safe on the hook hot path per Rule 11.2.
//
// Hot path (already in active cadence — the common case):
//   1 relaxed atomic store + 1 relaxed atomic load + 1 branch ≈ 5 ns.
//
// Cold path (idle → active transition, fires at most once per idle cycle):
//   + 1 workerSignalFn_ invocation = MainThreadWorker::Signal which acquires
//   an uncontended mutex (~30-50 ns) and notifies the worker CV (~50-200 ns).
//   Worker then runs workHandler → RetuneCadenceIfNeeded → SetTickInterval.
//
// The currentTickIntervalMs_ gate is essential: without it, every keystroke
// would Signal the worker → workHandler runs SyncConfigFromSharedState +
// DrainClassifyOnWorker (~1-10 ms cold cache) on every key — wasted work
// since cadence is already at the active 200 ms.
void HookEngine::MarkActivity() noexcept {
    lastActivityTickMs_.store(GetTickCount64(), std::memory_order_relaxed);
    if (currentTickIntervalMs_.load(std::memory_order_relaxed) != NextKey::kTickActiveMs) {
        if (workerSignalFn_) workerSignalFn_();
    }
}

// Adaptive-tick — recomputes the desired tick interval from the elapsed time
// since last MarkActivity and republishes it to MainThreadWorker if it
// changed. No-op when the cadence is already correct. Called from BOTH:
//   * OnTickPoll (worker tick path) — handles the regular age-out from
//     active → idle as time passes.
//   * The workHandler wired in main.cpp (worker signal path) — handles
//     the resume from idle → active when MarkActivity signals the worker.
// Both call sites are on the worker thread; this method is not safe to call
// on the hook thread (tickRetuneFn_ may take MainThreadWorker's mutex).
void HookEngine::RetuneCadenceIfNeeded() noexcept {
    // Anti-Dorion v2 (+ A1): while a KNOWN hook-hijacker (Dorion) is foreground
    // AND the user is recently active, pin the tick at the detector's required
    // cadence (40ms) — the detector samples physical state at ~40ms, so backing
    // off mid-Dorion would lose keystrokes before recovery. For every other app
    // (incl. plain Chrome/Electron, which don't hijack) use the activity-driven
    // adaptive cadence — no 40ms pin, so no wasted wakeups.
    //
    // Deep-idle STOP (2026-05-30) OVERRIDES the pin: after kIdleStopThreshMs
    // with no input, ComputeTickInterval returns 0 (STOP) and we let the worker
    // park even under chromium. The detector only matters while the user is
    // typing; with no input there is nothing to protect, and a perpetual 40ms
    // poll just keeps pages warm and blocks the working-set trim (the whole
    // point of this change — v2.1.24 idle parity). The next keystroke
    // (MarkActivity → Signal) resumes the cadence and re-arms the detector
    // before that key is processed — a ≤1-2 key anti-Dorion residual on resume,
    // the same trade the detector already makes. Below the stop threshold the
    // chromium pin still wins (instant hijack recovery while active).
    constexpr auto kChromiumActiveInterval = std::chrono::milliseconds(40);
    const std::uint64_t now = GetTickCount64();
    const std::uint64_t lastAct = lastActivityTickMs_.load(std::memory_order_relaxed);
    const std::uint64_t idleMs = (now > lastAct) ? (now - lastAct) : 0;
    const auto desired =
        (isKnownHijackerApp_.load(std::memory_order_acquire)
         && idleMs < NextKey::kIdleStopThreshMs)
            ? kChromiumActiveInterval
            : NextKey::ComputeTickInterval(idleMs);
    const auto desiredMs = static_cast<std::uint32_t>(desired.count());
    if (desiredMs != currentTickIntervalMs_.load(std::memory_order_relaxed)) {
        currentTickIntervalMs_.store(desiredMs, std::memory_order_relaxed);
        if (tickRetuneFn_) tickRetuneFn_(desired);
    }
}

void HookEngine::OnFocusChangedSyncOnWorker(HWND triggerHwnd) {
    // Doctrine §12.5 exemption #1: OnTickPoll's PID-change branch already
    // runs on the worker thread, so it calls this directly without the
    // latch+signal hop. WinEventProc producers go through OnFocusChanged →
    // DrainClassifyOnWorker → here.
    //
    // P2c fix (2026-05-19) preserved: SettingsDialog is the project's "live
    // config bus" — every toggle bumps configGeneration in SharedState
    // immediately. Running QuickSync at focus-change time means a user who
    // toggles in Settings then clicks back to the target app sees the
    // toggle apply BEFORE typing the first character. Wave 2 made
    // QuickSync's slow path lock-free (atomics + RCU publish); 3.6
    // additionally routes the heap-allocating slow body through the
    // worker (this thread), so no Rule 11.2 violation is possible here.
    QuickSyncFromSharedState();

    FocusOwner::ConfigContext ctx{
        config_.load(std::memory_order_acquire),
        configSnapshot_.load(std::memory_order_acquire),
        static_cast<int>(globalCodeTable_.load(std::memory_order_acquire)),
        static_cast<int>(globalInputMethod_.load(std::memory_order_acquire)),
    };
    auto cls = std::make_shared<const FocusClassification>(
        focus_.Classify(triggerHwnd, ctx));
    if (!cls->hwndOpaque) return;  // sentinel: nothing to apply
    lifecycle_.Mailbox().Post(HookCommand::kFocusChanged, std::move(cls));
}

/// Replace on-screen text by diffing previousComposition_ vs newText.
///
/// ## U+202F "needEmpty" mechanism (skipEmptyChar_ == false)
///
/// Some Win32 apps swallow BS at certain cursor positions (start of line, empty
/// field, after autocomplete selection in browsers). To guarantee BS always
/// deletes something, we insert U+202F (NARROW NO-BREAK SPACE) as a "bait"
/// character before the BS sequence, then include one extra BS to remove it:
///
///   [insert U+202F] → [BS × (n+1)] → [type new chars]
///
/// U+202F is chosen because:
///   - It is a real Unicode character that apps must insert into the text buffer
///   - It is NOT U+0020 (regular space), so it doesn't trigger word commit
///   - It is narrow/invisible in most fonts, minimizing visual flicker
///
/// This mechanism is ONLY safe for apps that reliably insert U+202F into their
/// text buffer. Apps that ignore or filter it will receive n+1 BS for n chars,
/// deleting one extra character and permanently desyncing previousComposition_.
///
/// Apps with skipEmptyChar_=true (block reinjectVk, skip U+202F bait):
///   - Electron/Console: also get split dispatch — selected by the factory
///     (WindowClassification.isElectron / .isConsole → SplitDispatchInjector
///     with sleepMs=6 / 5 respectively). Sprint 2 D3 lifted the dispatch
///     branching out of HookEngine into the injector layer.
///   - GPU-rendered apps (Zed): batch dispatch via Win32SendInputInjector
///     (single-process, no IPC reorder).
///
/// Wave 2 — Pipeline::IBackwardEditExecutor adapter. Thin wrapper so
/// `BackwardEditFeature` (in `src/core/pipeline/`) can delegate the backward
/// edit through an interface without coupling to the full HookEngine class.
/// Wave 3+ will split this into pure-diff (feature) + Stage A–E execute path.
void HookEngine::ExecuteReplace(std::wstring_view newText,
                                std::uint16_t reinjectVk) {
    ReplaceComposition(std::wstring{newText}, static_cast<DWORD>(reinjectVk));
}

/// Wave 3 — Pipeline::ICommitUndoExecutor adapter. Delegates to the FSM
/// body (renamed `HandleCommitUndoFsm` to disambiguate from this override).
/// vnMode is read fresh from vietnameseMode_ atomic — slightly different
/// from the legacy inline call site that took vnMode as a stack-local
/// param, but byte-equivalent because vietnameseMode_ writers are async
/// to the hook thread (main thread on toggle / config reload).
NextKey::Pipeline::CommitUndoOutcome HookEngine::HandleCommitUndo(
    std::uint16_t vkCode) {
    const bool vnMode = vietnameseMode_.load(std::memory_order_acquire);
    const KeyOutcome out =
        HandleCommitUndoFsm(static_cast<DWORD>(vkCode), vnMode);
    switch (out) {
        case KeyOutcome::Eat:         return NextKey::Pipeline::CommitUndoOutcome::Eat;
        case KeyOutcome::Pass:        return NextKey::Pipeline::CommitUndoOutcome::Pass;
        case KeyOutcome::Fallthrough: return NextKey::Pipeline::CommitUndoOutcome::Fallthrough;
    }
    return NextKey::Pipeline::CommitUndoOutcome::Fallthrough;  // defensive
}

/// Wave 4b — Pipeline::IMacroExecutor adapter. Owns macro tracking +
/// dispatch. Body transcribed 1:1 from pre-W4b HandlePreDispatch:
///   - EN-mode block (pre-W4b lines 1512-1547)
///   - VN-mode tracking (1559-1572)
///   - VN SkipMacro hotkey (1602-1613)
///   - VN expansion (1615-1624)
/// Logic unchanged; the feature pipeline now owns the call site at step 2d.
/// Reads vnMode/macroOn/macroEng/configSnapshot/hotkeys atomics internally.
NextKey::Pipeline::MacroOutcome HookEngine::HandleMacro(
    std::uint16_t vkCode,
    bool shift, bool capsLock, bool ctrl, bool alt, bool win) {
    const DWORD vk = static_cast<DWORD>(vkCode);
    const bool vnMode = vietnameseMode_.load(std::memory_order_acquire);
    const bool macroOn = macroEnabled_.load(std::memory_order_acquire);
    const auto cfgSnap = configSnapshot_.load(std::memory_order_acquire);
    const bool hasMacros = cfgSnap && !cfgSnap->macroTable.empty();
    auto hotkeysSnap = hotkeys_.load(std::memory_order_acquire);
    const uint32_t currentMods = ComputeModMask(ctrl, shift, alt, win);

    // EN mode: only engages when macroOn && macroEng. Always returns Pass
    // (caller passes the key to OS as English) — except macro-expand which
    // can return Eat.
    if (!vnMode) {
        const bool macroEng = macroInEnglish_.load(std::memory_order_acquire);
        if (!(macroOn && macroEng)) {
            return NextKey::Pipeline::MacroOutcome::Fallthrough;
        }
        // Track macro keys in English mode.
        if (vk >= 0x41 && vk <= 0x5A) {
            const bool upper = shift != capsLock;  // XOR
            rawMacroBuffer_ += upper ? static_cast<wchar_t>(vk)
                                      : towlower(static_cast<wchar_t>(vk));
        } else if (hotkeysSnap
                   && hotkeysSnap->Matches(NextKey::Intent::SkipMacro, vk, currentMods,
                                           /*isDoubleTap=*/false, /*keyUp=*/false)
                   && rawMacroBuffer_.empty()) {
            tempMacroOff_ = true;
            return NextKey::Pipeline::MacroOutcome::Pass;
        } else if (IsCommitTrigger(vk) && !tempMacroOff_) {
            const wchar_t triggerChar = VkToMacroChar(vk);
            if (triggerChar > L' ') rawMacroBuffer_ += triggerChar;
            if (!rawMacroBuffer_.empty() && IsMacroTrigger(vk)) {
                auto result = TryExpandMacro(triggerChar);
                if (result == MacroResult::ExpandedEatTrigger) {
                    return NextKey::Pipeline::MacroOutcome::Eat;
                }
                if (result == MacroResult::ExpandedPassTrigger) {
                    if (dispatcher_.SynthEventsPending() > 0) {
                        InjectKey(vk);
                        return NextKey::Pipeline::MacroOutcome::Eat;
                    }
                    return NextKey::Pipeline::MacroOutcome::Pass;
                }
            } else if (!IsMacroTrigger(vk)) {
                // Disabled trigger still marks word boundary — clear buffer.
                rawMacroBuffer_.clear();
                tempMacroOff_ = false;
            }
        } else if (vk == VK_BACK && !rawMacroBuffer_.empty()) {
            rawMacroBuffer_.pop_back();
        } else if (!(vk >= 0x41 && vk <= 0x5A) && !IsCommitTrigger(vk)) {
            rawMacroBuffer_.clear();
            tempMacroOff_ = false;
        }
        // EN mode always passes to OS unless macro ate the key above.
        return NextKey::Pipeline::MacroOutcome::Pass;
    }

    // VN mode tracking — accumulate alpha + commit-trigger chars when macros loaded.
    if (macroOn && hasMacros) {
        if (vk >= 0x41 && vk <= 0x5A) {
            const bool upper = shift != capsLock;  // XOR
            rawMacroBuffer_ += upper ? static_cast<wchar_t>(vk)
                                      : towlower(static_cast<wchar_t>(vk));
        } else if (IsCommitTrigger(vk)) {
            const wchar_t ch = VkToMacroChar(vk);
            if (ch > L' ') rawMacroBuffer_ += ch;  // Printable non-space chars
        }
    }

    // VN SkipMacro hotkey — empty engine + empty buffer marks "skip next macro".
    if (macroOn && hasMacros
        && hotkeysSnap
        && hotkeysSnap->Matches(NextKey::Intent::SkipMacro, vk, currentMods,
                                /*isDoubleTap=*/false, /*keyUp=*/false)
        && engine_ && engine_->Count() == 0 && rawMacroBuffer_.empty()) {
        tempMacroOff_ = true;
        HOOK_LOG(L"  tempMacroOff: enabled by Esc");
        return NextKey::Pipeline::MacroOutcome::Pass;
    }

    // VN macro expansion — runs on commit trigger when buffer non-empty.
    if (macroOn && hasMacros && !tempMacroOff_
        && IsMacroTrigger(vk) && !rawMacroBuffer_.empty()) {
        const wchar_t triggerChar = VkToMacroChar(vk);
        auto result = TryExpandMacro(triggerChar);
        if (result == MacroResult::ExpandedEatTrigger) {
            return NextKey::Pipeline::MacroOutcome::Eat;
        }
        if (result == MacroResult::ExpandedPassTrigger) {
            if (dispatcher_.SynthEventsPending() > 0) {
                InjectKey(vk);
                return NextKey::Pipeline::MacroOutcome::Eat;
            }
            return NextKey::Pipeline::MacroOutcome::Pass;
        }
    }

    return NextKey::Pipeline::MacroOutcome::Fallthrough;
}

/// Wave 4a — Pipeline::IEscRestoreRawExecutor adapter. Resolves the
/// hotkey registry (CancelComposition intent) + composition-active gate
/// internally, then delegates to TryEscRestoreRaw. Mirrors the inline
/// check that previously lived at HandlePreDispatch lines 1578-1582
/// (now removed). MOD-CANCEL secondary site at line 1981 still calls
/// TryEscRestoreRaw directly — different trigger flow (modifier release),
/// out of W4a scope.
NextKey::Pipeline::EscRestoreOutcome HookEngine::TryEscRestore(
    std::uint16_t vkCode,
    bool shift, bool ctrl, bool alt, bool win) {
    auto hotkeysSnap = hotkeys_.load(std::memory_order_acquire);
    if (!hotkeysSnap) {
        return NextKey::Pipeline::EscRestoreOutcome::Fallthrough;
    }
    const uint32_t mods =
        (shift ? NextKey::kModShift : 0u) |
        (ctrl  ? NextKey::kModCtrl  : 0u) |
        (alt   ? NextKey::kModAlt   : 0u) |
        (win   ? NextKey::kModWin   : 0u);
    const bool hotkeyMatch = hotkeysSnap->Matches(
        NextKey::Intent::CancelComposition,
        static_cast<uint32_t>(vkCode),
        mods,
        /*isDoubleTap=*/false,
        /*keyUp=*/false);
    const bool hasLiveComposition = (engine_ && engine_->Count() > 0);
    const bool hasPrimedCommit =
        (commitState_.IsPrimed()) &&
        !commitState_.StackEmpty() &&
        !commitState_.StackTop().rawInput.empty();
    if (hotkeyMatch && (hasLiveComposition || hasPrimedCommit)) {
        const KeyOutcome legacy = TryEscRestoreRaw();
        return (legacy == KeyOutcome::Eat)
            ? NextKey::Pipeline::EscRestoreOutcome::Eat
            : NextKey::Pipeline::EscRestoreOutcome::Fallthrough;
    }
    return NextKey::Pipeline::EscRestoreOutcome::Fallthrough;
}

/// Wave 2 — pipeline dispatch entry point used by the six call-sites that
/// previously invoked `ReplaceComposition` directly. Builds the per-keystroke
/// session view + KeyContext, hands them to coordinator_, drains the channel.
/// Modifier flags are placeholders (false) in W2 — BackwardEditFeature does
/// not read them; future PreEngine features will need real values plumbed
/// down from ProcessKeyDown.
void HookEngine::DispatchCoordinator(DWORD vkCode, DWORD reinjectVk,
                                      const std::wstring& composition) {
    std::wstring_view rawSnapshot = engine_ ? engine_->PeekRawView() : std::wstring_view{};
    NextKey::Pipeline::HookCompositionSession session(
        previousComposition_, composition, rawSnapshot);
    NextKey::Pipeline::KeyContext keyCtx{
        static_cast<std::uint16_t>(vkCode),
        L'\0',
        false, false, false, false, false,
        &session,
        static_cast<std::uint16_t>(reinjectVk)
    };
    coordinator_.HandleKeyAtStage(
        NextKey::Pipeline::Stage::PostEngine, keyCtx, outputChannel_);
    (void)outputChannel_.DrainBatch();  // W2: feature delegates synchronously, batch is empty (no-alloc drain).
}

/// Wave 3 PR 3.3 — outer shell only. Computes the prefix diff against
/// `previousComposition_` (engine state) + encodes for non-Unicode code
/// tables + updates `previousComposition_` / `previousEncodedWidths_`,
/// then delegates the actual SendInput / RichEdit-retry / clipboard
/// fallback orchestration to `dispatcher_`. Detection logic + injector
/// publish lives in OnFocusChanged (focus_-driven).
void HookEngine::ReplaceComposition(const std::wstring& newText, DWORD reinjectVk) {
    VKEY_ASSERT_HOOK_THREAD();
    PERF_SCOPE(::NextKey::Perf::Stage::Replace);
    HWND target = GetInputTarget();
    if (!target) {
        previousComposition_ = newText;
        return;
    }

    // Find common prefix at Unicode level — only replace what actually changed.
    size_t commonLen = 0;
    size_t minLen = (std::min)(previousComposition_.size(), newText.size());
    while (commonLen < minLen && previousComposition_[commonLen] == newText[commonLen]) {
        commonLen++;
    }

    const CodeTable ct = currentCodeTable_.load(std::memory_order_acquire);

    // ── Non-Unicode code table path ──
    if (ct != CodeTable::Unicode) {
        // Calculate backspace count from encoded widths of chars being replaced.
        size_t backspaceCount = 0;
        for (size_t i = commonLen; i < previousEncodedWidths_.size(); ++i) {
            backspaceCount += previousEncodedWidths_[i];
        }

        // Convert new chars to encoded form.
        std::wstring encodedToSend;
        std::vector<uint8_t> newWidths;
        for (size_t i = commonLen; i < newText.size(); ++i) {
            auto enc = CodeTableConverter::ConvertChar(newText[i], ct);
            encodedToSend += enc.units[0];
            if (enc.count == 2) encodedToSend += enc.units[1];
            newWidths.push_back(enc.count);
        }

        HOOK_LOG(L"  ReplaceComposition[encoded]: prev='%s' new='%s' common=%zu BS=%zu encodedLen=%zu reinjectVk=0x%02X",
                 previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
                 encodedToSend.size(), reinjectVk);

        // Encoded path: no retry-loop, no clipboard fallback, no reinjectVk
        // prepend (preserves pre-Wave-3-PR-3.3 semantics — reinjectVk was
        // logged but never acted upon in the encoded branch).
        if (backspaceCount > 0 || !encodedToSend.empty()) {
            (void)dispatcher_.ReplaceRaw(backspaceCount,
                                          std::wstring_view(encodedToSend));
        }

        // Update widths: keep [0..commonLen), append newWidths.
        previousEncodedWidths_.resize(commonLen);
        previousEncodedWidths_.insert(previousEncodedWidths_.end(),
                                      newWidths.begin(), newWidths.end());
        previousComposition_ = newText;
        return;
    }

    // ── Unicode path ──
    size_t backspaceCount = previousComposition_.size() - commonLen;
    std::wstring toSend = newText.substr(commonLen);

    HOOK_LOG(L"  ReplaceComposition: prev='%s' new='%s' common=%zu BS=%zu send='%s' reinjectVk=0x%02X",
             previousComposition_.c_str(), newText.c_str(), commonLen, backspaceCount,
             toSend.c_str(), reinjectVk);

    // Dispatcher handles the full retry+fallback orchestration:
    //   1. IsSyncReplaceChannel? → RichEdit 30ms retry-loop, fall through on exhaust.
    //   2. ShouldUseClipboard → TryEditMessagePaste + clipboard fallback chain
    //      (BS-adjusts when reinjectVk != 0).
    //   3. Generic SendInput with optional reinjectVk prepend.
    dispatcher_.ReplaceUnicode(backspaceCount,
                                std::wstring_view(toSend),
                                static_cast<std::uint16_t>(reinjectVk));

    previousComposition_ = newText;
}

HookEngine::KeyOutcome HookEngine::TryEscRestoreRaw() {
    auto inj = dispatcher_.GetInjector();

    // Path 1: live composition (existing behavior).
    if (engine_->Count() > 0) {
        const size_t composedCount = engine_->Count();
        const std::wstring raw = engine_->PeekRaw();
        if (raw.empty()) return KeyOutcome::Fallthrough;
        HOOK_LOG(L"  EscRestoreRaw[live]: bs=%zu raw='%ls'", composedCount, raw.c_str());
        bool injOk;
        { PERF_SCOPE(::NextKey::Perf::Stage::Injector);
          injOk = inj->Replace(composedCount, std::wstring_view(raw)); }
        if (!injOk) {
            HOOK_LOG(L"  EscRestoreRaw[live]: injector reported partial delivery");
            // Don't reset on failure — next user action recovers via normal flow.
            return KeyOutcome::Fallthrough;
        }
        engine_->Reset();
        rawMacroBuffer_.clear();
        tempMacroOff_ = false;
        return KeyOutcome::Eat;
    }

    // Path 2: post-BS (engine empty, raw snapshot in commitStack top).
    // Engine empty here; rawInput preserved in commitStack_ from CommitComposition
    // snapshot. CancelCommitUndo clears stack (single-word scope per design 2026-05-17).
    if (!commitState_.IsPrimed() || commitState_.StackEmpty()) {
        return KeyOutcome::Fallthrough;
    }
    if (GetTickCount() - commitState_.ReadyTime() > kCommitUndoTimeoutMs) {
        HOOK_LOG(L"  EscRestoreRaw[post-BS]: Primed expired (elapsed > %ums)", kCommitUndoTimeoutMs);
        CancelCommitUndo();
        return KeyOutcome::Fallthrough;
    }
    const auto& top = commitState_.StackTop();
    if (top.rawInput.empty()) return KeyOutcome::Fallthrough;
    // Primed: trailing commit-trigger already deleted by user's BS. BS count covers
    // the committed body only. Non-Unicode code tables (TCVN3, VNI-Win) encode each
    // wchar_t into multiple bytes — mirror HandleBackspace's width-sum logic.
    size_t bsCount = top.text.size();
    if (currentCodeTable_.load(std::memory_order_acquire) != CodeTable::Unicode) {
        bsCount = 0;
        for (auto w : top.widths) bsCount += w;
    }
    HOOK_LOG(L"  EscRestoreRaw[post-BS]: bs=%zu raw='%ls' text='%ls'",
             bsCount, top.rawInput.c_str(), top.text.c_str());
    if (!inj->Replace(bsCount, std::wstring_view(top.rawInput))) {
        HOOK_LOG(L"  EscRestoreRaw[post-BS]: injector reported partial delivery");
        return KeyOutcome::Fallthrough;
    }
    CancelCommitUndo();
    rawMacroBuffer_.clear();
    tempMacroOff_ = false;
    return KeyOutcome::Eat;
}


// ═══════════════════════════════════════════════════════════
// Modifier Tracking — feeds double-Alt + layout-change detection
// ═══════════════════════════════════════════════════════════

void HookEngine::TrackModifier(DWORD vkCode, bool isDown) {
    // Snapshot the *other* modifiers before mutating, so a fresh modifier-down
    // that joins an existing hold latches modComboSeen_ (combo contamination).
    // Auto-repeat re-enters with the matching modXxxDown_ already true, so the
    // inner `!modXxxDown_` guard prevents a held key from contaminating itself.
    const bool ctrlWasDown  = modCtrlDown_;
    const bool shiftWasDown = modShiftDown_;
    const bool altWasDown   = modAltDown_;
    const bool winWasDown   = modWinDown_;
    switch (vkCode) {
        case VK_LCONTROL: case VK_RCONTROL:
            if (isDown && !modCtrlDown_) {
                modCtrlDown_ = true; otherKeyPressed_ = false;
                if (shiftWasDown || altWasDown || winWasDown) modComboSeen_ = true;
            } else if (!isDown) modCtrlDown_ = false;
            break;
        case VK_LSHIFT: case VK_RSHIFT:
            if (isDown && !modShiftDown_) {
                modShiftDown_ = true; otherKeyPressed_ = false;
                if (ctrlWasDown || altWasDown || winWasDown) modComboSeen_ = true;
            } else if (!isDown) modShiftDown_ = false;
            break;
        case VK_LMENU: case VK_RMENU:
            if (isDown && !modAltDown_) {
                modAltDown_ = true; otherKeyPressed_ = false;
                if (ctrlWasDown || shiftWasDown || winWasDown) modComboSeen_ = true;
            } else if (!isDown) modAltDown_ = false;
            break;
        case VK_LWIN: case VK_RWIN:
            if (isDown && !modWinDown_) {
                modWinDown_ = true; otherKeyPressed_ = false;
                if (ctrlWasDown || shiftWasDown || altWasDown) modComboSeen_ = true;
            } else if (!isDown) modWinDown_ = false;
            break;
    }
    // Session ends — and the combo latch clears — only once every modifier is
    // up. This runs AFTER ProcessKeyUp's modifier-release matching reads the
    // latch, so the trailing release of a combo is still seen as contaminated.
    if (!isDown && !modCtrlDown_ && !modShiftDown_ && !modAltDown_ && !modWinDown_) {
        modComboSeen_ = false;
    }
}

// ═══════════════════════════════════════════════════════════
// Commit Trigger Check
// ═══════════════════════════════════════════════════════════

void HookEngine::InjectKey(DWORD vkCode) {
    // Wave 3 PR 3.3 — delegated to dispatcher. Re-entrant gate (sending_)
    // + watchdog timestamp (lastSynthSendTime_ only — lastRealSynthTime_
    // stays unchanged because InjectKey is re-injection, NOT typing).
    dispatcher_.InjectKey(static_cast<std::uint16_t>(vkCode));
}

bool HookEngine::IsCommitTrigger(DWORD vkCode) {
    // Space, Enter, Escape
    if (vkCode == VK_SPACE || vkCode == VK_RETURN || vkCode == VK_ESCAPE) return true;

    // Tab
    if (vkCode == VK_TAB) return true;

    // Arrow keys
    if (vkCode >= VK_LEFT && vkCode <= VK_DOWN) return true;
    if (vkCode == VK_HOME || vkCode == VK_END ||
        vkCode == VK_PRIOR || vkCode == VK_NEXT) return true;

    // Number keys (0-9)
    if (vkCode >= 0x30 && vkCode <= 0x39) return true;

    // Numpad keys
    if (vkCode >= VK_NUMPAD0 && vkCode <= VK_DIVIDE) return true;

    // OEM keys (punctuation)
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_3) return true;
    if (vkCode >= VK_OEM_4 && vkCode <= VK_OEM_8) return true;
    if (vkCode == VK_OEM_PLUS || vkCode == VK_OEM_COMMA ||
        vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PERIOD) return true;

    // Delete, Insert
    if (vkCode == VK_DELETE || vkCode == VK_INSERT) return true;

    return false;
}

bool HookEngine::IsOemPunctVk(DWORD vkCode) {
    if (vkCode >= VK_OEM_1 && vkCode <= VK_OEM_3) return true;
    if (vkCode >= VK_OEM_4 && vkCode <= VK_OEM_8) return true;
    if (vkCode == VK_OEM_PLUS || vkCode == VK_OEM_COMMA ||
        vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PERIOD) return true;
    return false;
}

bool HookEngine::IsMacroTrigger(DWORD vkCode) const {
    // If not a commit trigger natively, it shouldn't trigger macro either
    if (!IsCommitTrigger(vkCode)) return false;

    // Sprint 1 D6: snapshot the RCU shared_ptr once for the call. The loaded
    // shared_ptr keeps the config object alive even if a writer (ApplyConfig
    // / ReloadFromToml) publishes a new config mid-call — safe internal
    // consistency without stateMutex_ acquisition on the hook hot path.
    auto cfg = config_.load(std::memory_order_acquire);
    if (vkCode == VK_SPACE) return cfg->macroTriggerSpace;
    if (vkCode == VK_RETURN) return cfg->macroTriggerEnter;
    if (vkCode == VK_TAB) return cfg->macroTriggerTab;

    // Direction / Navigation
    if (vkCode >= VK_LEFT && vkCode <= VK_DOWN) return cfg->macroTriggerDir;
    if (vkCode == VK_HOME || vkCode == VK_END ||
        vkCode == VK_PRIOR || vkCode == VK_NEXT) return cfg->macroTriggerDir;

    return true; // Numbers, Punctuation, Esc, etc. default to true if they are commit triggers
}

HookEngine::MacroResult HookEngine::TryExpandMacro(wchar_t triggerChar) {
    Win32CaseMapper mapper;
    // Phase 3c: macro table comes from the RCU snapshot. The shared_ptr
    // local keeps the table alive for the duration of Macro::Plan even
    // if a worker thread republishes mid-call.
    auto snap = configSnapshot_.load(std::memory_order_acquire);
    Macro::PlanInputs inputs{
        .rawMacroBuffer        = rawMacroBuffer_,
        .previousComposition   = previousComposition_,
        .previousEncodedWidths = previousEncodedWidths_,
        .macroTable            = snap->macroTable,
        .macroCrossCommit      = macroCrossCommit_,
        .currentCodeTable      = currentCodeTable_.load(std::memory_order_acquire),
        .autoCapsEnabled       = autoCapsMacro_.load(std::memory_order_acquire),
        .triggerChar           = triggerChar,
        .clipboardThreshold    = kMacroClipboardThreshold,
    };
    auto plan = Macro::Plan(inputs, mapper);
    if (!plan.matched) return MacroResult::NoMatch;

    auto inj = dispatcher_.GetInjector();

    if (plan.useClipboard) {
        if (plan.bsCount > 0) {
            (void)dispatcher_.ReplaceRaw(plan.bsCount, std::wstring_view{});
        }
        auto clipText = Macro::ExpandEscapesForClipboard(plan.expansion);
        dispatcher_.ClipboardPasteText(clipText);
        HOOK_LOG(L"  TryExpandMacro: clipboard paste %zu chars (raw %zu)",
                 clipText.size(), plan.expansion.size());
    } else {
        std::size_t pendingBs = plan.bsCount;
        for (const auto& s : Macro::BuildSegments(plan.expansion, currentCodeTable_.load(std::memory_order_acquire))) {
            if (s.isReturn) {
                if (pendingBs > 0) {
                    (void)dispatcher_.ReplaceRaw(pendingBs, std::wstring_view{});
                    pendingBs = 0;
                }
                dispatcher_.InjectKey(VK_RETURN);
            } else if (!s.text.empty()) {
                (void)dispatcher_.ReplaceRaw(pendingBs, std::wstring_view(s.text));
                pendingBs = 0;
            }
        }
        if (pendingBs > 0) {
            (void)dispatcher_.ReplaceRaw(pendingBs, std::wstring_view{});
        }
    }

    ClearWordState();
    CancelCommitUndo();
    return plan.isPartOfMacro ? MacroResult::ExpandedEatTrigger
                              : MacroResult::ExpandedPassTrigger;
}

wchar_t HookEngine::VkToMacroChar(DWORD vkCode) noexcept {
    // Translate VK → character with the current modifier state, so Shift/Caps/
    // AltGr yield the actual typed char (e.g. Shift+VK_OEM_PERIOD on US → '>'
    // instead of the unshifted '.'). Uses the foreground window's layout so
    // macros match what the target app would receive.
    //
    // Modifier state: GetKeyboardState is not reliable from a low-level hook
    // thread (LL hooks don't feed our message queue), so we build a minimal
    // key-state snapshot from GetAsyncKeyState for the modifiers ToUnicodeEx
    // actually consults.
    BYTE keyState[256] = {};
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) keyState[VK_SHIFT]   = 0x80;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keyState[VK_CONTROL] = 0x80;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) keyState[VK_MENU]    = 0x80;
    if (GetKeyState(VK_CAPITAL) & 0x0001)      keyState[VK_CAPITAL] = 0x01;

    UINT scan = MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC);
    HWND fg = GetForegroundWindow();
    HKL layout = GetKeyboardLayout(fg ? GetWindowThreadProcessId(fg, nullptr) : 0);

    // wFlags bit 2 (0x4) = "do not change the keyboard state" — required so
    // ToUnicodeEx doesn't advance pending dead-key state. Win10 1607+.
    wchar_t buf[4] = {};
    int result = ToUnicodeEx(vkCode, scan, keyState, buf, 4, 0x4, layout);
    if (result > 0) {
        return static_cast<wchar_t>(towlower(buf[0]));
    }

    // result <= 0: dead key (-1) or no translation (0). Fall back to the
    // unshifted mapping — matches the pre-ToUnicodeEx behavior for these keys.
    UINT ch = MapVirtualKeyW(vkCode, MAPVK_VK_TO_CHAR);
    return ch ? static_cast<wchar_t>(towlower(static_cast<wchar_t>(ch))) : 0;
}

// ═══════════════════════════════════════════════════════════
// Phase 2a — Hook-thread command drain
//
// Single-writer invariant: every mutation of the 16 composition-state
// fields must happen on the hook thread. Producers on other threads use
// `mailbox_.Post(bit, ...)`; this drain consumes from the LL hook callback
// (Rule 11.4 step 5 barrier) and from the pump's WM_APP_HOOK_COMMAND
// handler. Dispatch order follows the design doc: kConfigApply first
// (may rebuild engine_), then kFocusChanged (resets composition), then
// kTickPoll, then kToggleVN.
//
// Phase 2a ships the infrastructure ONLY. No producer calls Post yet —
// existing OnFocusChanged / OnTickPoll / ApplyConfig / ToggleVietnameseMode
// still mutate inline as before. Phase 2b/c migrate them onto this channel
// one writer at a time. Until then DrainHookCommands always returns early
// (mailbox bits=0).
// ═══════════════════════════════════════════════════════════

void HookEngine::DrainHookCommands() {
    // Phase 2d: re-entrancy guard. Trips a Debug assertion if a drain
    // handler somehow re-enters DrainHookCommands — that's the
    // "ApplyFoo() called something that called DrainHookCommands again"
    // bug pattern, which would corrupt mailbox bit state silently.
    HookCommandMailbox::DrainScope scope(lifecycle_.Mailbox());

    const std::uint32_t bits = lifecycle_.Mailbox().DrainBits();
    // Phase 3f: even if no fresh bits, a previous drain may have deferred
    // the config apply (engine was busy). Re-check on every drain so the
    // apply lands as soon as the engine empties.
    const bool hadDeferredApply = deferredConfigApply_.load(std::memory_order_acquire);
    if (!bits && !hadDeferredApply) return;

    // kConfigApply: latch the request; the actual apply runs at the tail
    // of this function once we know whether the engine is busy. We DO
    // NOT call ApplyConfigOnHookThread mid-drain anymore — see P3f note.
    if (bits & HookCommand::kConfigApply) {
        deferredConfigApply_.store(true, std::memory_order_release);
    }
    if (bits & HookCommand::kFocusChanged) ApplyFocusOnHookThread(lifecycle_.Mailbox().ConsumePendingFocus());
    if (bits & HookCommand::kTickPoll)     ApplyTickPollOnHookThread();
    if (bits & HookCommand::kToggleVN)     ApplyToggleVNOnHookThread();

    // Phase 3f — guard the config apply against mid-word reset.
    // ApplyConfigOnHookThread destroys engine_ and creates a fresh one;
    // if the user has uncommitted input (engine_->Count() > 0), running
    // the apply now would either (a) commit a partial word visibly or
    // (b) drop the partial input. Both are user-visible quirks. Defer
    // until the engine empties naturally — typically the next keystroke
    // after a word commit, occasionally a backspace-to-empty or focus
    // change. Focus change (ApplyFocusOnHookThread above) calls
    // ResetComposition which zeros the count, so deferred applies often
    // land on the same drain when triggered by a focus event.
    if (deferredConfigApply_.load(std::memory_order_acquire)
        && engine_ && engine_->Count() == 0) {
        // exchange(false) — defensive over load+store: even though
        // DrainScope guarantees single-drain-at-a-time today, a future
        // Phase 5 split could fragment the drain across classes. The
        // CAS-style swap makes "I'm the one consuming this latch"
        // explicit regardless of drain serialisation.
        if (deferredConfigApply_.exchange(false, std::memory_order_acq_rel)) {
            ApplyConfigOnHookThread();
        }
    }
}

// Phase 2b — focus apply runs on the hook thread (called from
// DrainHookCommands). All composition-state writes that used to live in
// OnFocusChanged moved here. The cls parameter is the pre-computed
// classification snapshot produced by ClassifyFocusedWindow on main.
//
// Must stay fast (Rule 11.2) — no syscalls beyond the cheap ones already
// listed in the design's "may only mutate composition state + atomic
// stores" contract. Heavy work (ClassifyWindow, GetExeNameForHwnd,
// IsWebView2App, CreateToolhelp32Snapshot) is in ClassifyFocusedWindow,
// not here.
void HookEngine::ApplyFocusOnHookThread(std::shared_ptr<const FocusClassification> cls) {
    VKEY_ASSERT_HOOK_THREAD();
    if (!cls || !cls->hwndOpaque) return;

    const auto cfg = config_.load(std::memory_order_acquire);

    HWND activeHwnd = reinterpret_cast<HWND>(cls->hwndOpaque);

    // PID update moved (2026-05-29): OnTickPoll now updates lastForegroundPid_
    // defensively BEFORE calling OnFocusChangedSyncOnWorker (v2.1.24 pattern),
    // which breaks the WebView2 poll-loop that commit 87a560f tried to fix by
    // moving the update here. Updating PID at function entry blocked Bug 1's
    // natural fallback (notepad++ launch with only helper events relies on the
    // poll detecting a stale PID to re-classify against GetForegroundWindow
    // once the main window settles). PID is now updated below at the end of
    // the real-focus path — see comment near isExcludedApp_ store.

    // Reset composition + per-word state. These were the Rule 11.3-violating
    // writes from main pre-Phase-2b.
    ResetComposition();
    tempEngineOff_ = false;
    autoCapState_ = AutoCapState::Idle;
    // Clear the combo latch defensively: a focus switch (Alt+Tab) can swallow a
    // modifier-up, stranding modComboSeen_ true and suppressing the next genuine
    // single-modifier tap until every modifier is observed up again. (#189)
    modComboSeen_ = false;

    // Reconcile our own modifier state after desktop switch / Win+L lock
    if (modCtrlDown_ && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) modCtrlDown_ = false;
    if (modShiftDown_ && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) modShiftDown_ = false;
    if (modAltDown_ && !(GetAsyncKeyState(VK_MENU) & 0x8000)) modAltDown_ = false;
    if (modWinDown_ && !(GetAsyncKeyState(VK_LWIN) & 0x8000) && !(GetAsyncKeyState(VK_RWIN) & 0x8000)) modWinDown_ = false;
    otherKeyPressed_ = false;

    if (hotkeyManager_) {
        hotkeyManager_->ReconcileModifiers();
    }

    // Per-app cached flags — single release-store pair with the hot-path
    // acquire-loads in ProcessKeyDown / HandleAlphaKey. Wave 3 PR 3.3:
    // owned by OutputDispatcher (atomic readers go through getter API).
    dispatcher_.SetSkipEmptyChar(cls->localSkipEmpty);
    dispatcher_.SetUseClipboardPaste(cls->localClipboard);
    // Formula-segment tracking arms only for spreadsheet hosts (Excel). The
    // preceding ResetComposition already cleared formulaState_ + pushed
    // SetSuppressBait(false), so the new host starts with the bait enabled.
    hostIsFormulaCapable_ = cls->localFormulaHost;

    // IOutputInjector swap — RCU publish so in-flight HandleAlphaKey reads
    // see either the old or new injector cleanly.
    {
        NextKey::Output::WindowClassification c{};
        c.isRichEditD2DPT   = cls->localEditMsg;
        c.isElectron        = cls->localElectronApp;
        c.isConsole         = cls->isConsole;
        c.isChromium        = cls->localNeedBait;
        c.useClipboard      = cls->localUseClipboardInjector;
        // Per-app "send method = compatibility split" (sendMethod 2/3). 0 when
        // the focused app has no such override → factory keeps the Win32 path.
        c.forcedSplitSleepMs = cls->localForcedSplitSleepMs;
        c.forceEmReplaceSel = cls->localForceEmReplaceSel;
        auto newInjector = NextKey::Output::Create(c);
        // Re-apply user setting on the freshly-built injector so the new
        // host inherits the live "BS giữ chữ khi có gợi ý" value (factory
        // doesn't know about it). Without this, a focus change resets the
        // suggestKeepChars flag to default false until the next ApplyConfig.
        // Use the outer `cfg` loaded at function entry — no re-load needed.
        newInjector->SetSuggestKeepChars(cfg->suggestKeepChars);
        // Seed the new injector's bait-suppress flag from the current segment
        // state. ResetComposition() ran earlier in this focus apply, so
        // baitSuppressed_ is the safe default (false) here — a focus change
        // starts a fresh cell; a genuine mid-formula state is re-derived on the
        // next keystroke by UpdateFormulaSegment. Set explicitly (rather than
        // rely on the injector's default) so the contract is visible at the swap.
        newInjector->SetSuppressBait(baitSuppressed_);
        dispatcher_.SetInjector(std::move(newInjector));
    }

    HOOK_LOG(L"  AppDetect: console=%d skipEmpty=%d electron=%d webview2=%d bait=%d clipboard=%d editMsg=%d useClipInj=%d splitSleepMs=%d",
             cls->isConsole ? 1 : 0, cls->localSkipEmpty ? 1 : 0, cls->localElectronApp ? 1 : 0,
             cls->isWebView2 ? 1 : 0, cls->localNeedBait ? 1 : 0, cls->localClipboard ? 1 : 0,
             cls->localEditMsg ? 1 : 0, cls->localUseClipboardInjector ? 1 : 0,
             cls->localForcedSplitSleepMs);

    // Anti-Dorion (hook-only): cache whether the foreground is a Chromium-class
    // host so LowLevelMouseProc can reinstall-on-click. Placed HERE (before the
    // skipAppTracking guard at :3350 and the config-gate below) deliberately —
    // empirically the function returns early for Dorion's main window between
    // those guards (23:10 vs 09:14 logs proved it). Moving the store past the
    // guards leaves the flag stale and breaks mouse-down reinstall for Dorion.
    // Helper-window transient flips self-correct on the next real focus event.
    const bool wasChromiumClass = isChromiumClassApp_.exchange(
        cls->localElectronApp || cls->isBrowser, std::memory_order_acq_rel);
    const bool isChromiumClass = cls->localElectronApp || cls->isBrowser;
    // A1: the expensive responses (burst + 40ms pin + mouse-click reinstall)
    // gate on the narrower known-hijacker flag — only Dorion installs a
    // competing hook. The detector itself stays universal (isChromiumClass).
    const bool isKnownHijacker = cls->isKnownHijacker;
    const bool wasKnownHijacker =
        isKnownHijackerApp_.exchange(isKnownHijacker, std::memory_order_acq_rel);
    // Anti-Dorion v2: gate the hijack detector + retune MainThreadWorker's
    // cadence on flag transitions.
    //   - Detector gate: Poll() returns immediately when flag is false →
    //     no ghost-key work outside chromium sessions (Invariant 3 & 4).
    //   - Cadence retune: when the KNOWN-HIJACKER flag flips, signal the worker
    //     so its NEXT workHandler runs RetuneCadenceIfNeeded promptly — pinning
    //     to (or releasing from) the detector's 40ms cadence without waiting a
    //     full tick. Also signal on the broader chromium flip so the universal
    //     detector starts/stops polling promptly.
    if (hijackDetector_) hijackDetector_->SetChromiumClassActive(isChromiumClass);
    if ((wasChromiumClass != isChromiumClass || wasKnownHijacker != isKnownHijacker)
        && workerSignalFn_) {
        workerSignalFn_();
    }

    // Anti-Dorion v2 PRIMARY path — fire a reinstall burst when focus first
    // lands on a chromium-class app. The single focus-time reinstall above
    // (PostReinstallHooks reason=chromium, triggered earlier in the focus
    // pipeline) often lands BEFORE Dorion's own LL hook install completes;
    // the burst covers the install window so at least one of our reinstalls
    // lands AFTER theirs → VKey at chain head, hook wins.
    //
    // Delays chosen to clear the 500 ms chromium throttle in HookLifecycle:
    // the focus-time reinstall just ran at ~t=0, so a 300 ms burst step
    // gets SKIPPED (throttled). 600 / 1200 / 1800 ms put each step safely
    // past the throttle boundary (≥ 500 ms gap from any prior reinstall)
    // so all three execute. Total span 1200 ms covers the typical Dorion
    // renderer-init window (100-500 ms) plus headroom.
    //
    // On focus-out, cancel any pending burst to avoid spurious reinstalls
    // when the user is no longer typing into Dorion. See
    // docs/plans/2026-05-28-anti-dorion-detector-inject-design.md.
    if (wasKnownHijacker != isKnownHijacker && reinstallBurstScheduler_) {
        // A1: gate on the known-hijacker transition (Dorion), not every browser.
        // Always cancel pending callbacks on transition — clean slate.
        // Cancel is cheap (atomic generation bump) so unconditional call OK.
        reinstallBurstScheduler_->Cancel();
        if (isKnownHijacker) {
            HOOK_LOG(L"  BurstReinstall: scheduling 600/1200/1800ms burst (known hijacker fg entered)");
            reinstallBurstScheduler_->Schedule(
                static_cast<uint32_t>(REINSTALL_REASON_CHROMIUM),
                {600, 1200, 1800});
        }
    }

    // RefreshFocusCache uses GetFocusedChildHwnd (AttachThreadInput) which
    // is cheap (~µs). Safe on hook thread.
    if (cls->localClipboard || cls->localEditMsg) {
        focus_.RefreshFocusCache(activeHwnd);
    } else {
        focus_.InvalidateFocusCache();
    }

    // CJK layout check — GetKeyboardLayout is kernel-cached, single µs.
    CheckLayoutChange();

    // Split state SmartSwitch (2026-05-26): activeExe_ tracks any focused
    // exe (including helper windows like dock panels / SearchHost / tray);
    // lastRealExe_ tracks only non-skipAppTracking transitions. This split
    // resolves the tension between toggle (wants "user's current app", so
    // notepad++ all-helper case attributes correctly) and SAVE (wants
    // "last real app", so helper-event detours don't poison the previous
    // real app's entry).
    //
    // Gates for activeExe_ update:
    //   - non-empty exeName (classifier failed to resolve → skip)
    //   - cls->pid != 0 (GetWindowThreadProcessId failed → skip)
    //   - cls->pid != GetCurrentProcessId() (our own tray/menu → skip)
    if (!cls->exeName.empty() && cls->pid != 0 &&
        cls->pid != GetCurrentProcessId()) {
        focus_.SetActiveExe(cls->exeName);
    }

    // Helper HWNDs: SAVE/RESTORE skipped (mode shouldn't flip on transient
    // dock-panel, tray icon, splash, init helper, or shell focus events).
    // activeExe_ above is already updated so the next toggle / non-skip
    // focus event sees the right app.
    //
    // Bug 1 (notepad++ launches with helper-only events, mode never
    // restores) is fixed via the OnTickPoll PID poll: helper events
    // here return WITHOUT updating lastForegroundPid_, the poll detects
    // a stale PID on its next tick (~200 ms later), updates the PID
    // defensively, and re-fires OnFocusChanged against the foreground
    // window — by which point the main window has typically settled
    // and runs the real-focus SmartSwitch path below. This is the
    // pre-2026-05-21 (v2.1.24) pattern; see commit 87a560f notes +
    // docs/plans/2026-05-28-smart-switch-persistence-design.md §4.
    if (cls->skipAppTracking) return;

    // Short-circuit when no per-app feature needs tracking. Phase 3c
    // reads the override-map presence from the RCU snapshot — same data
    // the cls fields were resolved against in Classify.
    {
        auto snap = configSnapshot_.load(std::memory_order_acquire);
        const bool noOverrides = !snap
            || (snap->appEncodingOverrides.empty()
                && snap->appInputMethodOverrides.empty());
        if (!cfg->smartSwitch && !cfg->excludeApps && !cfg->tsfApps && noOverrides) return;
    }

    if (cls->exeName.empty()) return;

    const bool wasExcluded = isExcludedApp_.load(std::memory_order_acquire);
    const bool wasTsfApp   = isTsfApp_.load(std::memory_order_acquire);
    const bool wasForcedV  = isForcedVnApp_.load(std::memory_order_acquire);

    // Smart switch SAVE for the previous real app — captured BEFORE we
    // advance lastRealExe_ below. Uses lastRealExe_, NOT activeExe_:
    // a helper-event detour right before this non-skip event would have
    // moved activeExe_ to the helper's exe, while lastRealExe_ correctly
    // still points to the app whose mode the engine state corresponds to.
    const std::wstring oldLastReal = focus_.LastRealExe();
    // !wasForcedV: a forced-V app's mode is locked to V, never the user's
    // choice — don't persist it into appModeMap or it poisons the remembered
    // preference (the SAVE writes the forced 'true', not a real toggle).
    if (cfg->smartSwitch && !oldLastReal.empty() && !wasExcluded && !wasForcedV) {
        if (focus_.AppModeMap().size() >= kMaxSmartSwitchEntries) {
            focus_.AppModeMap().clear();
            HOOK_LOG(L"  SmartSwitch: map cap %zu hit, cleared", kMaxSmartSwitchEntries);
        }
        const bool savedMode = vietnameseMode_.load(std::memory_order_acquire);
        focus_.AppModeMap()[oldLastReal] = savedMode;
        focus_.Smart().SetAppMode(oldLastReal, savedMode);
        focus_.PublishAppModesSnapshot();           // RCU publish (alloc + atomic_store)
        focus_.MarkAppModeDirty();
        focus_.SetLastDirtyTs(GetTickCount64());    // worker debounce input
    }

    // Advance lastRealExe_ to the new real app (auto-shifts previousExe_
    // for the encoding-override fallback chain).
    focus_.SetLastRealExe(cls->exeName);

    // Real-focus PID update — moved here from function entry (v2.1.24
    // pattern restored, see comment near top). Helper-only events that
    // hit the skipAppTracking early-return above LEAVE PID stale on
    // purpose so the OnTickPoll defensive update can re-fire OnFocusChanged
    // once the app's main window settles to foreground (Bug 1 fallback).
    if (cls->pid) focus_.SetLastForegroundPid(cls->pid);

    isExcludedApp_.store(cls->isExcluded, std::memory_order_release);
    isTsfApp_.store(cls->isTsf, std::memory_order_release);
    // Store forced-V cache flag at the SAME site as the others, BEFORE the
    // excluded/tsf early-returns below — so switching excluded↔forced-V leaves
    // the flag consistent. forcedVnPid_ feeds the toggle-lock PID check.
    isForcedVnApp_.store(cls->isForcedVietnamese, std::memory_order_release);
    if (cls->isForcedVietnamese) forcedVnPid_.store(cls->pid, std::memory_order_release);

    HOOK_LOG(L"  Engine: %s for '%s' (tsf_feature=%d, in_tsf_list=%d, excluded=%d)",
             cls->isTsf ? L"TSF (hook passthrough)" : L"HOOK",
             focus_.LastRealExe().c_str(),
             cfg->tsfApps ? 1 : 0,
             cls->isTsf ? 1 : 0,
             cls->isExcluded ? 1 : 0);

    // SharedState TSF flag bridge — idempotent via SetOrClearFlag in main.
    if (tsfModeCallback_) {
        const bool tsfReadonly = !cls->isTsf && !cls->isExcluded;
        if (cls->isTsf != wasTsfApp) {
            HOOK_LOG(L"  TSF_ACTIVE flag: %s → %s",
                     wasTsfApp ? L"true" : L"false", cls->isTsf ? L"true" : L"false");
        }
        tsfModeCallback_(cls->isTsf, tsfReadonly);
    }

    if (cls->isExcluded) {
        excludedPid_.store(cls->pid, std::memory_order_release);
        HOOK_LOG(L"  ExcludeApps: '%s' is excluded, passthrough (pid=%u)",
                 focus_.LastRealExe().c_str(), cls->pid);
        if (!wasExcluded) NotifyModeChange();
        return;
    }


    // Per-app hard-V lock WINS over smart-switch: force V on focus regardless of
    // any remembered preference. (D2: on LEAVING a forced-V app the normal
    // smart-switch restore for the *next* app applies — handled on that focus.)
    if (cls->isForcedVietnamese) {
        if (!vietnameseMode_.load(std::memory_order_acquire)) {
            vietnameseMode_.store(true, std::memory_order_release);
            HOOK_LOG(L"  ForceVN: locked Vietnamese for '%s'", focus_.LastRealExe().c_str());
            NotifyModeChange();
        }
    } else if (cfg->smartSwitch) {
        // Smart switch restore for the new app — uses lastRealExe_ (just set
        // above to cls->exeName).
        auto it = focus_.AppModeMap().find(focus_.LastRealExe());
        if (it != focus_.AppModeMap().end()) {
            const bool curMode = vietnameseMode_.load(std::memory_order_acquire);
            if (it->second != curMode) {
                vietnameseMode_.store(it->second, std::memory_order_release);
                HOOK_LOG(L"  SmartSwitch: restored %s for '%s'",
                         it->second ? L"Vietnamese" : L"English",
                         focus_.LastRealExe().c_str());
                NotifyModeChange();
            }
        } else {
            HOOK_LOG(L"  SmartSwitch: inherit %s for unknown '%s'",
                     vietnameseMode_.load(std::memory_order_acquire)
                         ? L"Vietnamese" : L"English",
                     focus_.LastRealExe().c_str());
        }
    }

    // Leaving an excluded OR forced-V app — effective mode changed even if
    // vietnameseMode_ didn't move, and CJK auto-switch was gated off while
    // locked. Replay the layout check so a suppressed CJK transition can
    // re-evaluate now that the lock has cleared.
    if (wasExcluded || wasForcedV) {
        const bool wasSuppressed = focus_.LayoutSuppressed();
        OnLayoutChanged(focus_.CachedIsCompatLayout());
        if (wasSuppressed == focus_.LayoutSuppressed()) NotifyModeChange();
    }

    if (cls->isTsf) {
        HOOK_LOG(L"  TsfApps: '%s' uses TSF engine, hook passthrough",
                 focus_.LastRealExe().c_str());
        // #195: mode is fully RESOLVED here (the force-V / smart-switch blocks above
        // ran), so re-publish to activate the VKey TSF profile with the correct mode.
        // The early tsfModeCallback_ near the top fires BEFORE that resolution, so on
        // entry from an English app it would read the previous mode and skip the
        // activation — that is why "Khoa E/V theo app" did not auto-apply to TSF.
        // Focus-driven only (never the OnTickPoll TSF_TIP_ACTIVE/Win+Space monitor),
        // so a deliberate switch to the US keyboard is preserved. VN-gated and
        // idempotent inside the callback.
        if (tsfModeCallback_) {
            tsfModeCallback_(/*tsfActive=*/true, /*tsfReadonly=*/false);
        }
        return;
    }

    // Per-app encoding override (target value pre-resolved in Classify
    // against the on-main global to avoid a cross-thread read of
    // globalCodeTable_ here).
    {
        const CodeTable targetTable = static_cast<CodeTable>(cls->targetCodeTable);
        if (targetTable != currentCodeTable_.load(std::memory_order_acquire)) {
            currentCodeTable_.store(targetTable, std::memory_order_release);
            HOOK_LOG(L"  AppOverride: encoding=%d for '%s'",
                     static_cast<int>(targetTable),
                     focus_.LastRealExe().c_str());
        }
    }

    // Per-app input method override (same pattern as encoding). Recreate
    // engine_ on change — the only heap allocation on this path.
    {
        const InputMethod targetMethod = static_cast<InputMethod>(cls->targetMethod);
        if (targetMethod != currentMethod_.load(std::memory_order_acquire)) {
            currentMethod_.store(targetMethod, std::memory_order_release);
            TypingConfig engineConfig = *config_.load(std::memory_order_acquire);
            engineConfig.inputMethod = targetMethod;
            engine_ = EngineFactory::Create(engineConfig);
            HOOK_LOG(L"  AppOverride: inputMethod=%d for '%s'",
                     static_cast<int>(targetMethod),
                     focus_.LastRealExe().c_str());
        }
    }
}

// Phase 2c — ToggleVietnameseMode body, migrated to the hook thread.
//
// Single-writer note: this runs from the drain only. The mailbox
// coalesces multiple Posts before drain into one drained bit
// (HookCommandMailboxTest §PostSameBitMultipleTimesDrainReturnsOnce),
// so rapid hotkey mashing collapses to one toggle per drain cycle.
// User-visible behaviour: same as before — sub-keystroke responsive.
void HookEngine::ApplyToggleVNOnHookThread() {
    VKEY_ASSERT_HOOK_THREAD();
    const auto cfg = config_.load(std::memory_order_acquire);
    // Excluded-app gate. PID check vs cached excludedPid_ distinguishes
    // "genuinely in excluded app" (block toggle) from "stale flag, user
    // already left" (force VN). Both atomic stores below are safe on
    // hook thread now that we're single-writer.
    if (cfg->excludeApps && isExcludedApp_.load(std::memory_order_acquire)) {
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg) GetWindowThreadProcessId(fg, &fgPid);
        const DWORD cachedPid = excludedPid_.load(std::memory_order_acquire);
        if (fgPid == cachedPid && cachedPid != 0) {
            HOOK_LOG(L"  ToggleVN: BLOCKED (excluded pid=%u)", cachedPid);
            return;
        }
        // Different PID — user already left excluded app, flag is stale.
        // Force VN: user pressed toggle expecting V mode, having perceived
        // the excluded app as English.
        isExcludedApp_.store(false, std::memory_order_release);
        vietnameseMode_.store(true, std::memory_order_release);
        HOOK_LOG(L"  ToggleVN: stale excluded → forced Vietnamese (fg pid=%u)", fgPid);
        NotifyModeChange();
        if (cfg->beepOnSwitch) MessageBeep(MB_OK);
        return;
    }
    // Forced-V (per-app hard-V) gate — symmetric to the excluded gate. While
    // genuinely in a forced-V app the toggle is locked (D1). Silent on block to
    // match the excluded gate (D5). Stale PID → user already left → clear and
    // fall through to a normal toggle.
    if (cfg->excludeApps && isForcedVnApp_.load(std::memory_order_acquire)) {
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg) GetWindowThreadProcessId(fg, &fgPid);
        const DWORD cachedPid = forcedVnPid_.load(std::memory_order_acquire);
        if (fgPid == cachedPid && cachedPid != 0) {
            HOOK_LOG(L"  ToggleVN: BLOCKED (forced-V pid=%u)", cachedPid);
            return;
        }
        isForcedVnApp_.store(false, std::memory_order_release);
        HOOK_LOG(L"  ToggleVN: stale forced-V cleared (fg pid=%u)", fgPid);
    }

    // Commit pending composition (skip if CJK-suppressed — engine inactive).
    if (!focus_.LayoutSuppressed() && engine_->Count() > 0) {
        CommitComposition();
    }
    CancelCommitUndo();
    digitLedWord_ = false;

    const bool newMode = !vietnameseMode_.load(std::memory_order_acquire);
    vietnameseMode_.store(newMode, std::memory_order_release);
    NEXTKEY_LOG(L"HookEngine: mode = %s (via drain)", newMode ? L"Vietnamese" : L"English");

    // Smart-switch save. Drop the pre-P2c GetForegroundWindow + GetExeNameForHwnd
    // fallback — those are Rule 11.2 forbidden on the hook thread (Toolhelp32
    // snapshot). Toggle uses activeExe_ (any focused window's exe, including
    // helper-window apps like notepad++ dock panels) so map writes attribute
    // to the app the user is interacting with even when the focus path
    // detours. If activeExe_ is empty here (startup before any focus event),
    // the next focus event sets it and the toggle takes effect on first save.
    if (cfg->smartSwitch && !focus_.ActiveExe().empty()) {
        focus_.AppModeMap()[focus_.ActiveExe()] = newMode;
        focus_.Smart().SetAppMode(focus_.ActiveExe(), newMode);
        focus_.PublishAppModesSnapshot();
        focus_.MarkAppModeDirty();
        focus_.SetLastDirtyTs(GetTickCount64());
    }

    if (cfg->beepOnSwitch) {
        MessageBeep(newMode ? MB_OK : MB_ICONASTERISK);
    }
    NotifyModeChange();

    // #195: a same-window E/V toggle changes no focus, so the focus-path callback
    // above never runs — re-publish here so the VKey TSF profile follows an explicit
    // toggle into Vietnamese while staying in a TSF app. VN-gated in the callback.
    if (isTsfApp_.load(std::memory_order_acquire) && tsfModeCallback_) {
        tsfModeCallback_(/*tsfActive=*/true, /*tsfReadonly=*/false);
    }
}

// P3e/P3f — config-apply drain handler. Wired into the kConfigApply mailbox
// bit posted by ReloadFromToml on the worker. Hook-thread side of the
// single-writer contract: this is where composition state mutations
// (currentMethod_ store, engine_ swap) actually run.
//
// CONTRACT (P3f): DrainHookCommands guarantees `engine_->Count() == 0`
// before calling this function. Any pending word is left intact in the
// engine until natural completion (commit, backspace-empty, focus
// reset); the drain latches kConfigApply via `deferredConfigApply_` and
// re-checks on every cycle. This avoids the chaos-stress visible quirk
// where a config reload landing mid-word committed a partial word (e.g.
// `uongs` → `uôngs` instead of `uống` because the engine was reset
// between `uong` and `s`).
//
// Pre-P3e (P2c→P3d): handler existed dormant; the worker's ReloadFromToml
// performed CommitComposition + `engine_ = Create()` inline. Under
// `-InjectConfigReloadMs 50` chaos, that race produced 11/55 failures —
// a UAF: worker swapped `engine_` while hook hot path held a raw
// pointer read. P3e moved the mutation here; P3f added the engine-busy
// gate at the drain.
//
// Why ALWAYS recreate (not just on method change): the engine internally
// stores a TypingConfig copy. modernOrtho / allowZwjf / spellCheckEnabled
// changes need a fresh engine for the new behavior to take effect. The
// drain's busy-gate means we don't recreate per chaos tick — we recreate
// once per word boundary, when applicable, regardless of how many bumps
// stacked up.
//
// Resolves the P0 single-writer-violation TODO item from the 2026-05-19
// review: `engine_` had two writer paths; this collapses to one (hook).
void HookEngine::ApplyConfigOnHookThread() {
    VKEY_ASSERT_HOOK_THREAD();
    auto cfg = config_.load(std::memory_order_acquire);
    if (!cfg) return;

    // Consume smart-switch off→on handoff (worker stashed loaded map in
    // ReloadFromToml). exchange()'ing with nullptr makes the consume
    // single-shot — a redundant ApplyConfig drain won't re-run the swap.
    if (auto loaded = pendingAppModeMap_.exchange(nullptr, std::memory_order_acq_rel)) {
        if (!focus_.Smart().IsConnected()) {
            (void)focus_.Smart().Create();
        }
        focus_.AppModeMap() = *loaded;
        if (!focus_.AppModeMap().empty()) {
            focus_.Smart().LoadFromMap(focus_.AppModeMap());
        }
        focus_.PublishAppModesSnapshot();
        HOOK_LOG(L"  SmartSwitch: off→on swap-in (%zu apps)",
                 focus_.AppModeMap().size());
    }

    // Resolve target inputMethod considering per-app override (Phase 3c
    // snapshot reader). activeExe_ is hook-owned (set in
    // ApplyFocusOnHookThread); reading it here is single-threaded safe.
    auto snap = configSnapshot_.load(std::memory_order_acquire);
    InputMethod targetMethod = cfg->inputMethod;
    if (snap && !focus_.ActiveExe().empty()) {
        auto it = snap->appInputMethodOverrides.find(focus_.ActiveExe());
        if (it != snap->appInputMethodOverrides.end()) targetMethod = it->second;
    }

    // P3f: caller (DrainHookCommands) gates on engine_->Count() == 0, so
    // CommitComposition would be a no-op. Skip it to keep this handler
    // purely focused on the engine swap.
    currentMethod_.store(targetMethod, std::memory_order_release);
    TypingConfig engineConfig = *cfg;
    engineConfig.inputMethod = targetMethod;
    engine_ = EngineFactory::Create(engineConfig);

    HOOK_LOG(L"  ApplyConfig: engine recreated (method=%d, modernOrtho=%d, allowZwjf=%d)",
             static_cast<int>(targetMethod),
             cfg->modernOrtho ? 1 : 0,
             cfg->allowZwjf ? 1 : 0);
}

void HookEngine::ApplyTickPollOnHookThread() {
    VKEY_ASSERT_HOOK_THREAD();
    // CheckLayoutChange queries GetKeyboardLayout (kernel-cached, fast)
    // and may call OnLayoutChanged → layoutSuppressed_ writes + engine
    // commit. All hook-thread-safe.
    CheckLayoutChange();
}

}  // namespace NextKey
