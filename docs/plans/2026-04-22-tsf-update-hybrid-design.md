# TSF DLL Hybrid Update — Design

**Date**: 2026-04-22
**Status**: Design approved, implementation pending
**Scope**: How users update NexusKey when `NextKeyTSF.dll` is already loaded into other processes (Chrome, Word, Outlook, …).

## Problem

NexusKey ships a TSF Text Input Processor DLL (`NextKeyTSF.dll`). Once registered via `regsvr32`, Windows' TSF framework loads this DLL into **every process** that focuses an editable control. When a new version is released:

- `NexusKey.exe` can be killed and overwritten safely.
- `NextKeyTSF.dll`, however, is mapped (SEC_IMAGE) into dozens of live host processes — the file is locked via the image section.
- Even if the DLL file is replaced on disk, host processes keep running the *old* DLL code in their memory until they exit or the machine reboots.
- In that window, the old DLL reads a `SharedState` that the new EXE writes. Layout drift → corruption → host crash.

## Constraints

- Portable user-mode install (no admin, no MSI, no `MOVEFILE_DELAY_UNTIL_REBOOT`).
- TSF registration is per-user (`HKCU\Software\Classes\CLSID\...`).
- DLL path is derived from the EXE's directory (`GetTsfDllPath()`).
- Installer is launched as `NexusKey.exe /update <zip>` (or similar) — same binary, different mode.

## UX goal

**Hybrid update** (option D):
- EXE, engine, UI, config schema → updated immediately (take effect next keystroke).
- DLL → updated on disk immediately **when possible**; deferred to next reboot **when locked**.
- Banner notifies the user when a reboot is required to fully sync.

User expectation: matches Unikey / GoTiengViet / Microsoft IME — "restart to finish update" is an accepted pattern.

## Architecture

### 1. Update-time flow (`UpdateInstaller.cpp`)

Separate `NextKeyTSF.dll` from the bulk "move all .exe/.dll" logic. Other DLLs (`sciter.dll`, …) only load inside `NexusKey.exe` (killed at step 1), so normal overwrite works.

```
RunUpdateInstaller(zipPath):
  1. WaitForOtherProcesses(30_000)                      // existing
  2. Extract ZIP → _update_temp/                        // move earlier
  3. On extract failure → rollback from _old_version/   // existing
  4. For each file in _update_temp/:
       if filename == "NextKeyTSF.dll":
         HandleTsfDllReplace(src, exeDir)
       else:
         MoveFileW(live → _old_version) + CopyFileW(new → live)
  5. DeleteFileW(zipPath); remove_all(_update_temp)
  6. Launch NexusKey.exe → ExitProcess(0)

HandleTsfDllReplace(src, exeDir):
  auto live = exeDir / "NextKeyTSF.dll";
  auto parked = _old_version / ("NextKeyTSF.dll_" + timestamp);

  // D-b optimistic: try overwrite now. NTFS allows same-volume rename of
  // image-mapped DLL because the image section is opened FILE_SHARE_DELETE.
  if (MoveFileW(live, parked)) {
      CopyFileW(src, live);                   // new DLL on disk
      DeleteFileW(exeDir / "_pending_dll_update");  // clear stale marker
      return;
  }

  // Fallback: defer
  CopyFileW(src, exeDir / "NextKeyTSF.dll.pending");
  WriteMarker(exeDir / "_pending_dll_update", targetVersion);
```

**Rollback path** (step 3) must also remove any `.pending` file created in a prior abort.

**Marker file** `_pending_dll_update` contains the target version as plain text. Used to verify `.pending` is authentic, not user debris.

### 2. Startup-time handling (`main.cpp`)

Run **before** `CoInitializeEx` and any SharedState / TSF work.

```cpp
enum PendingDllState { kNone = 0, kSwapFailed = 1, kSwapDoneNeedsReboot = 2 };
int g_pendingDllReboot = kNone;

void ApplyPendingDllUpdate() noexcept {
    namespace fs = std::filesystem;
    auto exeDir = GetExeDirectory();
    auto pending = exeDir / L"NextKeyTSF.dll.pending";
    auto live    = exeDir / L"NextKeyTSF.dll";
    auto marker  = exeDir / L"_pending_dll_update";
    auto oldDir  = exeDir / L"_old_version";

    if (!fs::exists(pending)) {
        std::error_code ec; fs::remove(marker, ec);   // defensive cleanup
        return;
    }

    fs::create_directories(oldDir);
    auto parked = oldDir / (L"NextKeyTSF.dll_" + NowTimestampW());

    std::error_code ec1, ec2;
    fs::rename(live, parked, ec1);
    if (ec1) {                        // live still locked
        g_pendingDllReboot = kSwapFailed;
        return;
    }
    fs::rename(pending, live, ec2);
    if (ec2) {                        // rare: rollback
        fs::rename(parked, live, ec1);
        g_pendingDllReboot = kSwapFailed;
        return;
    }

    fs::remove(marker, ec1);
    g_pendingDllReboot = kSwapDoneNeedsReboot;
}
```

`CleanupOldUpdateFiles()` continues to run later; parked `.dll_<ts>` entries inside `_old_version/` will be deletable once every process that had the old DLL mapped has exited.

### 3. SharedState ABI gating (`src/core/ipc/SharedState.h`)

Append-only layout as primary rule, magic/version check as safety net.

```cpp
inline constexpr uint32_t kSharedStateMagic   = 0x53535853;  // 'NXSS'
inline constexpr uint32_t kSharedStateLayoutV = 1;           // bump only on break

struct alignas(16) SharedStateHeader {
    uint32_t magic;          // kSharedStateMagic
    uint32_t layoutVersion;  // kSharedStateLayoutV
    uint32_t sizeofStruct;   // sizeof(SharedState) at writer side
    uint32_t writerPid;      // debug
};

struct SharedState {
    SharedStateHeader header;

    // Existing fields — append only, never reorder or resize.
    std::atomic<uint32_t> flags;
    TypingConfigSnapshot  config;
    HookContextAnchor     anchor;
    // ...

    uint8_t reserved[1024];  // draw from this pool for future fields
};

static_assert(sizeof(SharedStateHeader) == 16, "header layout frozen");
static_assert(offsetof(SharedState, flags) == 16, "flags offset frozen");
```

**Why 1024 bytes reserved**: shared-memory mapping rounds up to page size (4 KB). As long as `sizeof(SharedState)` stays under 4 KB, reserve size has zero runtime cost. 1024 B gives ~5 years of append room, minimizing the frequency of forced `layoutVersion` bumps (which force a user reboot).

**Writer (EXE)** — `SharedStateManager::Initialize()`:
```cpp
state_->header.magic         = kSharedStateMagic;
state_->header.layoutVersion = kSharedStateLayoutV;
state_->header.sizeofStruct  = sizeof(SharedState);
state_->header.writerPid     = GetCurrentProcessId();
```

**Reader (DLL)** — first `EngineController::Initialize()`:
```cpp
bool CheckSharedStateAbi(const SharedState* s) noexcept {
    if (!s) return false;
    if (s->header.magic != kSharedStateMagic) return false;
    if (s->header.layoutVersion != kSharedStateLayoutV) return false;
    if (s->header.sizeofStruct < offsetof(SharedState, <lastRequiredField>)) return false;
    return true;
}
```

On mismatch: `abiOk_ = false` → `WantKey()` returns `false` → KeyEventSink passes every key through. User gets raw English typing in that host until they close it. No crash.

**Bump rules for `kSharedStateLayoutV`**:
- Add field into `reserved[]` → **no bump** (old DLL reads fewer fields; new EXE tolerates).
- Reorder / resize / retype an existing field → **bump**. Old DLL hits mismatch → passthrough.

**Back-channel for banner**: add flag bit `SharedFlags::TSF_ABI_MISMATCH`. DLL sets this bit on mismatch. EXE polls in SettingsDialog tick. Sticky bit is fine — EXE re-inits `flags` on each boot, so banner self-clears after reboot.

Why the flag bit over a dedicated atomic: reuses existing `InterlockedXor` / `SetOrClearFlag` infrastructure, `flags` currently uses ~5 bits of 32 (25+ free). Cost = 1 bit vs 4 bytes + new field.

### 4. UX (banner + tray)

| Condition | Message (VN) | Message (EN) |
|---|---|---|
| `g_pendingDllReboot == 1` | "Cập nhật chưa hoàn tất. Khởi động lại Windows để áp dụng phiên bản TSF mới." | "Update not finished. Restart Windows to apply the new TSF version." |
| `g_pendingDllReboot == 2` \|\| `flags & TSF_ABI_MISMATCH` | "Một vài ứng dụng đang chạy phiên bản cũ. Khởi động lại Windows để đồng bộ." | "Some apps still run the old version. Restart Windows to sync." |

**Surfaces**:
- Settings dialog (Sciter + Classic): full-width banner at top, `--color-warning-bg`, buttons "Restart now" (confirm → `ExitWindowsEx(EWX_REBOOT | EWX_RESTARTAPPS, ...)`) and "Later".
- Tray menu: item `⚠ Restart to finish update`.
- No automatic toast/popup — avoid nagging.

**Clear logic**:
- `g_pendingDllReboot` is a process-local global, reset on each EXE launch.
- `TSF_ABI_MISMATCH` cleared by `SharedStateManager::Initialize()` overwriting the flags field.
- Both reset post-reboot → banner self-heals.

**i18n strings to add** (`strings.js` + `Strings.cpp`):
- `S_UPDATE_BANNER_PENDING`
- `S_UPDATE_BANNER_MISMATCH`
- `S_UPDATE_BANNER_RESTART_NOW`
- `S_UPDATE_BANNER_LATER`

No "don't remind" option: reboot is genuinely needed; snooze would let users forget and hit mixed behavior across apps.

### 5. Testing & rollout

**Unit tests** (extend `SharedStateTest.cpp`, runs on Linux CI):
- `HeaderLayoutFrozen` — `static_assert` offsets of `flags`, `config`, `anchor`.
- `AbiCheckAcceptsCurrent` — fresh init passes.
- `AbiCheckRejectsWrongMagic` — corrupt magic → `false`.
- `AbiCheckRejectsWrongVersion` — bump version → `false`.
- `AbiCheckRejectsTooSmall` — sizeofStruct too small → `false`.

**Manual integration test** (document in `docs/update-test-plan.md`):
1. Build `v1.0` (layoutVersion = 1).
2. Build `v1.1` with intentional `layoutVersion = 2`.
3. Install v1.0, register TSF, open Word → TIP loaded.
4. Run update → verify:
   - Live `NextKeyTSF.dll` on disk is v1.1.
   - `_old_version/NextKeyTSF.dll_<ts>` parked copy exists.
   - Word still types (old DLL in RAM) but hits ABI mismatch → passthrough (English).
   - `TSF_ABI_MISMATCH` bit set.
   - Settings banner shows case-2 message.
5. Close and reopen Word → fresh load of v1.1 DLL → Vietnamese typing resumes.
6. Reboot → banner clears.

**Migration from current state**:
- Today's SharedState has no header; the shared memory is *live* state, not persistent config (TOML is).
- v1.1 `Initialize()` zero-fills then writes the new header — no upgrade logic needed.
- No production DLL predates this change (TSF is currently off), so no pre-header DLL exists to worry about.

**Rollout phases** (keeps blast radius small):

| Phase | What ships | TSF state |
|---|---|---|
| 1 | SharedState header + `reserved[1024]` | still off |
| 2 | Installer + startup swap logic | still off |
| 3 | Banner UI + i18n | still off |
| 4 | Enable TSF | ABI contract now active |

**Residual risks**:
- Antivirus holds a non-share-delete handle → installer rename fails → permanent `.pending` until reboot. Rare (Windows Defender uses share-delete). Banner covers it.
- User never reboots → banner persists → no crash, just annoyance. Not mitigated further.
- `_old_version/` grows across many updates without reboot → existing `CleanupOldUpdateFiles()` on each boot handles it.

## Open items deferred (not in this design)

- Detecting when TSF is registered but no host is actively using it → could shortcut straight to overwrite. YAGNI for now; optimistic `MoveFileW` already succeeds in that case.
- Auto-reboot scheduling (e.g., "reboot at 3am"): out of scope; user initiates.
- Cross-user install (`%ProgramFiles%`): requires admin + `MOVEFILE_DELAY_UNTIL_REBOOT`. Would need a separate design.
