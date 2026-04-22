# TSF DLL Hybrid Update — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the hybrid TSF DLL update mechanism so that future NexusKey releases can replace `NextKeyTSF.dll` in place when possible, defer the swap safely when locked, and protect the cross-version window with a SharedState ABI gate.

**Architecture:** Extend the existing `SharedState` magic/version/size gate with a larger reserved pool and three new flag bits (`TSF_ABI_MISMATCH`, `TSF_PENDING_DLL_SWAP`, `TSF_POST_UPDATE_REBOOT`). The DLL checks the gate on init: on mismatch it enters passthrough mode and sets its flag bit. The installer carves `NextKeyTSF.dll` out of the "move all .exe/.dll" bulk step and tries `MoveFileW` overwrite first, falling back to `NextKeyTSF.dll.pending`. The main EXE attempts the deferred swap early in its startup path and publishes the outcome into the two swap-state flags. Settings (which runs as a `--settings` subprocess) + Classic + tray all read those flags from shared memory — the only viable cross-process signal — and render a restart banner when any of the three bits is set.

**Tech Stack:** C++20, Win32, std::filesystem, std::atomic, Sciter.JS, Win32 menus, Google Test.

**Reference:** See `docs/plans/2026-04-22-tsf-update-hybrid-design.md` for the approved design.

**Rollout:** Phases 1–3 below ship with TSF still disabled. Phase 4 (enable TSF) is **out of scope for this plan** — it is gated on manual verification after Phases 1–3 ship.

---

## File map

### Create
- `src/app/system/PendingDllApply.cpp` — deferred-swap logic called from `WinMain`.
- `src/app/system/PendingDllApply.h` — header for the above.

### Modify
- `src/core/ipc/SharedState.h` — add `TSF_ABI_MISMATCH` flag, grow `reserved[]`, bump `CURRENT_VERSION`, add frozen-offset `static_assert`s, update size assertion.
- `src/tsf/EngineController.h` — add `abiOk_` member and one-time init gate.
- `src/tsf/EngineController.cpp` — on `!state.IsValid()` set `TSF_ABI_MISMATCH` flag and `abiOk_ = false`; `WantKey` gates on `abiOk_`.
- `src/app/system/UpdateInstaller.cpp` — carve `NextKeyTSF.dll` out of the bulk move step into `HandleTsfDllReplace`.
- `src/app/system/UpdateInstaller.h` — export `kTsfDllFilename` constant.
- `src/app/main.cpp` — call `ApplyPendingDllUpdate()` in the main-process path (after subprocess routes); publish the result into `TSF_PENDING_DLL_SWAP` / `TSF_POST_UPDATE_REBOOT` flags after `g_sharedState.Create()`.
- `src/core/Strings.h` / `src/core/Strings.cpp` — four new i18n keys for banner copy.
- `src/app/ui/shared/strings.js` — Sciter-side VN/EN translations.
- `src/app/ui/settings/settings.html` / `settings.css` / `settings.js` — banner DOM + CSS + wiring.
- `src/app/classic/ClassicSettingsDialog.cpp` — render banner above tabs; add "Restart now" button.
- `src/app/system/TrayIcon.cpp` — insert "Restart to finish update" item when banner active.
- `tests/SharedStateTest.cpp` — new ABI-mismatch tests and reserved-pool size assertions.

### Read-only reference (no edits)
- `docs/plans/2026-04-22-tsf-update-hybrid-design.md`
- `docs/CODING_RULES/5-struct-versioning.md`
- `src/core/ipc/SharedStateManager.cpp`

---

## Phase 1 — SharedState foundation

Goal: safe cross-version IPC. New EXE writes a struct an old DLL cannot misread silently.

### Task 1.1: Grow `reserved[]` to 1024 bytes and bump `CURRENT_VERSION`

**Files:**
- Modify: `src/core/ipc/SharedState.h:226-335`
- Modify: `tests/SharedStateTest.cpp` (size assertion)

- [ ] **Step 1: Write the failing test**

Add these tests at the bottom of the `SharedStateTest` class in `tests/SharedStateTest.cpp`:

```cpp
TEST_F(SharedStateTest, ReservedPool_IsAtLeast1024Bytes) {
    SharedState state{};
    EXPECT_GE(sizeof(state.reserved), 1024u);
}

TEST_F(SharedStateTest, CurrentVersion_IsAtLeast4) {
    EXPECT_GE(SharedState::CURRENT_VERSION, 4u);
}
```

- [ ] **Step 2: Run tests to confirm they fail**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="SharedStateTest.ReservedPool_IsAtLeast1024Bytes:SharedStateTest.CurrentVersion_IsAtLeast4"
```

Expected: both tests FAIL — current `reserved` is 21 bytes, `CURRENT_VERSION` is 3.

- [ ] **Step 3: Update the struct**

In `src/core/ipc/SharedState.h`:

Change the reserved-pool line from:
```cpp
    // ── Reserved for future expansion (21 bytes) ──
    uint8_t  reserved[21];
```
to:
```cpp
    // ── Reserved for future expansion (1024 bytes) ──
    // Draw from this pool for new fields; do NOT bump CURRENT_VERSION unless
    // resizing/reordering existing fields. See docs/CODING_RULES/5-struct-versioning.md.
    uint8_t  reserved[1024];
```

Change the `CURRENT_VERSION` constant from:
```cpp
    static constexpr uint32_t CURRENT_VERSION = 3;          // v3: added contextAnchor (phase 1 TSF readonly)
```
to:
```cpp
    static constexpr uint32_t CURRENT_VERSION = 4;          // v4: reserved pool grown to 1024 (hybrid DLL update headroom)
```

Update the `sizeof` assertion from:
```cpp
static_assert(sizeof(SharedState) == 100, "SharedState size changed — update structVersion");
```
to:
```cpp
// sizeof breakdown: 12 header + 4 epoch + 4 flags + 3 config + 3 featureFlags +
// 1 codeTable + 6 hotkey + 2 configGen/reserved0 + 1024 reserved + 44 anchor = 1103.
static_assert(sizeof(SharedState) == 1103, "SharedState size changed — update structVersion");
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="SharedStateTest.*"
```

Expected: all `SharedStateTest` tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/ipc/SharedState.h tests/SharedStateTest.cpp
git commit -m "feat(sharedstate): grow reserved pool to 1024B and bump version to 4

Makes future append-only field additions possible without forcing
further structVersion bumps (and the user-visible reboot they'd
require). Total struct size 100B → 1103B, still well under the 4KB
shared-memory page and the existing SharedStateManager mapping size
auto-expands via sizeof(SharedState)."
```

---

### Task 1.2: Add `TSF_ABI_MISMATCH` flag bit

**Files:**
- Modify: `src/core/ipc/SharedState.h:14-20`
- Modify: `tests/SharedStateTest.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/SharedStateTest.cpp`:

```cpp
TEST_F(SharedStateTest, SharedFlags_NewUpdateBitsDoNotCollide) {
    // Sanity: each new flag is non-zero and does not collide with existing bits
    // or with each other.
    constexpr uint32_t existing =
        SharedFlags::VIETNAMESE_MODE |
        SharedFlags::ENGINE_ENABLED  |
        SharedFlags::SPELL_CHECK     |
        SharedFlags::TSF_ACTIVE      |
        SharedFlags::TSF_READONLY;
    EXPECT_NE(SharedFlags::TSF_ABI_MISMATCH, 0u);
    EXPECT_NE(SharedFlags::TSF_PENDING_DLL_SWAP, 0u);
    EXPECT_NE(SharedFlags::TSF_POST_UPDATE_REBOOT, 0u);
    EXPECT_EQ(SharedFlags::TSF_ABI_MISMATCH & existing, 0u);
    EXPECT_EQ(SharedFlags::TSF_PENDING_DLL_SWAP & existing, 0u);
    EXPECT_EQ(SharedFlags::TSF_POST_UPDATE_REBOOT & existing, 0u);
    EXPECT_EQ(SharedFlags::TSF_ABI_MISMATCH & SharedFlags::TSF_PENDING_DLL_SWAP, 0u);
    EXPECT_EQ(SharedFlags::TSF_ABI_MISMATCH & SharedFlags::TSF_POST_UPDATE_REBOOT, 0u);
    EXPECT_EQ(SharedFlags::TSF_PENDING_DLL_SWAP & SharedFlags::TSF_POST_UPDATE_REBOOT, 0u);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="SharedStateTest.SharedFlags_NewUpdateBitsDoNotCollide"
```

Expected: compile FAIL — `TSF_ABI_MISMATCH` / `TSF_PENDING_DLL_SWAP` / `TSF_POST_UPDATE_REBOOT` unresolved.

- [ ] **Step 3: Add the flag**

In `src/core/ipc/SharedState.h`, inside `namespace SharedFlags`:

```cpp
namespace SharedFlags {
    constexpr uint32_t VIETNAMESE_MODE = 0x0001;
    constexpr uint32_t ENGINE_ENABLED  = 0x0002;
    constexpr uint32_t SPELL_CHECK     = 0x0004;
    constexpr uint32_t TSF_ACTIVE      = 0x0008;
    constexpr uint32_t TSF_READONLY    = 0x0010;
    constexpr uint32_t TSF_ABI_MISMATCH     = 0x0020;  // DLL detected SharedState layout mismatch
    constexpr uint32_t TSF_PENDING_DLL_SWAP = 0x0040;  // Main EXE: ApplyPendingDllUpdate returned kSwapFailed
    constexpr uint32_t TSF_POST_UPDATE_REBOOT = 0x0080;  // Main EXE: swap succeeded, hosts may still have old DLL mapped
}
```

The two extra bits exist because **Settings dialog runs as a subprocess** (`--settings` route in `main.cpp:202`), so any banner state in a main-process global is invisible to the dialog. SharedState is the only shared channel.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="SharedStateTest.SharedFlags_NewUpdateBitsDoNotCollide"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/ipc/SharedState.h tests/SharedStateTest.cpp
git commit -m "feat(sharedstate): add three TSF-update flag bits

TSF_ABI_MISMATCH: DLL sets on layout mismatch (disables itself).
TSF_PENDING_DLL_SWAP: Main EXE sets when startup swap failed.
TSF_POST_UPDATE_REBOOT: Main EXE sets when swap succeeded but hosts
may still have old DLL mapped.

All three drive the restart banner in the Settings subprocess, which
cannot see the main EXE's process-local state any other way."
```

---

### Task 1.3: Freeze field offsets with `static_assert`

**Files:**
- Modify: `src/core/ipc/SharedState.h` (after `sizeof` assertion)

- [ ] **Step 1: Add frozen-offset assertions**

Append to `src/core/ipc/SharedState.h`, immediately after the `sizeof(SharedState) == 1103` assertion:

```cpp
// Layout-freeze guards — failing any of these means a field was reordered or
// resized and CURRENT_VERSION MUST be bumped. See design doc
// (docs/plans/2026-04-22-tsf-update-hybrid-design.md § 3).
static_assert(offsetof(SharedState, magic) == 0,
              "magic must stay at offset 0");
static_assert(offsetof(SharedState, structVersion) == 4,
              "structVersion offset frozen");
static_assert(offsetof(SharedState, structSize) == 8,
              "structSize offset frozen");
static_assert(offsetof(SharedState, epoch) == 12,
              "epoch offset frozen");
static_assert(offsetof(SharedState, flags) == 16,
              "flags offset frozen");
static_assert(offsetof(SharedState, configGeneration) == 33,
              "configGeneration offset frozen");
// contextAnchor offset moves with reserved[] size. Pin it so any accidental
// field insert/reorder upstream gets caught at compile time.
static_assert(offsetof(SharedState, contextAnchor) == 1059,
              "contextAnchor offset frozen (must account for reserved[1024])");
```

- [ ] **Step 2: Run tests to verify compile and existing tests pass**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="SharedStateTest.*"
```

Expected: compiles, all PASS.

- [ ] **Step 3: Commit**

```bash
git add src/core/ipc/SharedState.h
git commit -m "feat(sharedstate): freeze public field offsets with static_assert

Any field reorder or resize that moves magic/epoch/flags/anchor will
now fail compilation, forcing the author to decide whether to bump
CURRENT_VERSION (and accept an ABI-mismatch reboot on next install)."
```

---

### Task 1.4: DLL enters passthrough on ABI mismatch and sets flag

**Files:**
- Modify: `src/tsf/EngineController.h:130-145` (add `abiOk_` member)
- Modify: `src/tsf/EngineController.cpp` — initial `Open` + `IsValid` site

- [ ] **Step 1: Inspect existing init to find the exact call site**

Run:
```bash
grep -n "IsValid\|ApplySharedState\|sharedState_.Open\|sharedState_.OpenReadWrite" src/tsf/EngineController.cpp
```

You should see the early-init block (around line 19) where `state.IsValid()` gates `ApplySharedState`. That is where we install the mismatch hook. Note the exact line numbers in your working copy before editing.

- [ ] **Step 2: Add the member variable**

In `src/tsf/EngineController.h`, alongside the other bool members (near line 138):

```cpp
    bool engineEnabled_ = true;     // ENGINE_ENABLED flag from SharedState
    bool tsfActive_ = false;        // TSF_ACTIVE flag from SharedState (foreground app in TSF list)
    bool vietnameseMode_ = true;    // VIETNAMESE_MODE flag from SharedState
    bool abiOk_ = true;             // false → SharedState layout mismatch, disable TSF for this process
```

- [ ] **Step 3: Handle the mismatch at init**

In `src/tsf/EngineController.cpp`, at the SharedState-open block (near line 19), replace the existing:

```cpp
        if (state.IsValid()) {
            lastEpoch_ = state.epoch;
            ApplySharedState(state);
        }
```

with:

```cpp
        if (state.IsValid()) {
            lastEpoch_ = state.epoch;
            ApplySharedState(state);
        } else if (sharedState_.IsConnected()) {
            // SharedState exists but layout doesn't match what this DLL was built
            // against — EXE was updated while this DLL is still mapped in a host
            // process. Signal the EXE (banner trigger) and disable TSF handling
            // in this process until the host is restarted or the machine reboots.
            abiOk_ = false;
            sharedState_.SetOrClearFlag(SharedFlags::TSF_ABI_MISMATCH, true);
        }
```

- [ ] **Step 4: Gate `WantKey` on `abiOk_`**

In `src/tsf/EngineController.cpp`, find `bool EngineController::WantKey(`:

```bash
grep -n "EngineController::WantKey" src/tsf/EngineController.cpp
```

Add, as the first statement inside the function body:

```cpp
    if (!abiOk_) return false;  // ABI mismatch — pass every key through
```

- [ ] **Step 5: Build and smoke-test**

Linux test suite does not exercise TSF, but it must still build all core code cleanly:
```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests
```

Expected: all tests PASS (no regressions).

- [ ] **Step 6: Commit**

```bash
git add src/tsf/EngineController.h src/tsf/EngineController.cpp
git commit -m "feat(tsf): passthrough on SharedState ABI mismatch + signal EXE

When the DLL loaded into a host process detects a layout mismatch
(e.g. EXE updated while host still has the old DLL mapped), disable
all key handling in this process and set TSF_ABI_MISMATCH so the EXE
can render the restart banner. No throws, no crashes — host just sees
raw English typing until restarted."
```

---

### Task 1.5: Integration test for ABI mismatch

**Files:**
- Modify: `tests/SharedStateTest.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/SharedStateTest.cpp`:

```cpp
TEST_F(SharedStateTest, FutureVersion_FailsIsValid_SoDllCanDetectMismatch) {
    // Simulate: new EXE wrote a struct with structVersion = CURRENT_VERSION + 1.
    // An old DLL reading this memory must see IsValid() == false so it can
    // disable itself and set TSF_ABI_MISMATCH.
    SharedState state{};
    state.InitDefaults();
    state.structVersion = SharedState::CURRENT_VERSION + 1;

    EXPECT_FALSE(state.IsValid())
        << "ABI gate relies on IsValid() rejecting future versions";
}

TEST_F(SharedStateTest, ShrunkStruct_FailsIsValid_SoDllCanDetectMismatch) {
    // Simulate: writer claimed a struct smaller than the header itself.
    SharedState state{};
    state.InitDefaults();
    state.structSize = 20;  // < 24 (minimum: header + epoch + flags + config)

    EXPECT_FALSE(state.IsValid());
}
```

- [ ] **Step 2: Run tests to verify they pass immediately**

`IsValid()` already implements both checks — these tests document the invariant the DLL relies on.

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests --gtest_filter="SharedStateTest.*"
```

Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/SharedStateTest.cpp
git commit -m "test(sharedstate): pin IsValid() rejection of future/shrunk structs

Documents the invariant the TSF DLL relies on for its ABI-mismatch
detection. Guards against anyone accidentally loosening IsValid() in
a future refactor."
```

---

## Phase 2 — Update flow

Goal: installer can land new DLL without nuking the old one out from under running hosts.

### Task 2.1: Carve `NextKeyTSF.dll` out of the bulk move step

**Files:**
- Modify: `src/app/system/UpdateInstaller.h`
- Modify: `src/app/system/UpdateInstaller.cpp`

- [ ] **Step 1: Export the DLL filename constant**

In `src/app/system/UpdateInstaller.h`, inside `namespace NextKey` before the function declarations, add:

```cpp
// Single source of truth for the TSF DLL filename. The update flow treats this
// file specially (see HandleTsfDllReplace) because it is routinely mapped into
// foreign host processes (Chrome, Word, Outlook, …) via Windows TSF.
inline constexpr const wchar_t* kTsfDllFilename = L"NextKeyTSF.dll";
```

- [ ] **Step 2: Add `HandleTsfDllReplace` in the anonymous namespace of `UpdateInstaller.cpp`**

In `src/app/system/UpdateInstaller.cpp`, inside the `namespace { ... }` block (near line 133, just before the closing `}  // namespace`), add:

```cpp
/// Replace a locked-prone DLL. Tries MoveFileW on the live copy first — NTFS
/// allows same-volume rename of image-mapped DLLs because the image section is
/// opened with FILE_SHARE_DELETE. If that succeeds, we copy the new file in
/// place and processes that haven't loaded the DLL yet pick up the new version
/// immediately.
///
/// If rename fails (rare: AV holding a non-share-delete handle), stash the new
/// DLL next to the old one with a `.pending` suffix and drop a marker file so
/// WinMain applies the swap on the next EXE launch.
///
/// Returns true if the live file on disk is now the new version.
bool HandleTsfDllReplace(const std::wstring& newDllSrc,
                         const std::wstring& exeDir,
                         const std::wstring& oldVersionDir) {
    namespace fs = std::filesystem;

    std::wstring liveDll = exeDir + L"\\" + kTsfDllFilename;

    // Park name: include a timestamp so repeated updates don't collide.
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t ts[32];
    swprintf_s(ts, L"_%04u%02u%02u_%02u%02u%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring parked = oldVersionDir + L"\\" + kTsfDllFilename + ts;

    std::error_code ec;
    fs::create_directories(oldVersionDir, ec);

    // Attempt optimistic overwrite.
    if (MoveFileW(liveDll.c_str(), parked.c_str())) {
        if (CopyFileW(newDllSrc.c_str(), liveDll.c_str(), FALSE)) {
            // Success: drop any stale pending marker so WinMain skips the swap path.
            DeleteFileW((exeDir + L"\\" + kTsfDllFilename + L".pending").c_str());
            DeleteFileW((exeDir + L"\\_pending_dll_update").c_str());
            return true;
        }
        // Copy failed — restore by renaming the parked copy back.
        MoveFileW(parked.c_str(), liveDll.c_str());
    }

    // Fallback: defer to boot. Write new DLL as `.pending` and drop a marker
    // file so WinMain knows to apply it (and so we can detect orphaned `.pending`
    // files created by a user manually).
    std::wstring pendingPath = exeDir + L"\\" + kTsfDllFilename + L".pending";
    CopyFileW(newDllSrc.c_str(), pendingPath.c_str(), FALSE);

    std::wstring markerPath = exeDir + L"\\_pending_dll_update";
    HANDLE hMarker = CreateFileW(markerPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hMarker != INVALID_HANDLE_VALUE) CloseHandle(hMarker);

    return false;
}
```

- [ ] **Step 3: Skip `NextKeyTSF.dll` in the bulk pre-move step**

Locate the "Move ALL .exe and .dll files" block in `RunUpdateInstaller` (around `src/app/system/UpdateInstaller.cpp:146-164`). Modify the inner body so the TSF DLL is explicitly excluded:

```cpp
        for (const auto& entry : fs::directory_iterator(exeDir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().wstring();
            if (_wcsicmp(ext.c_str(), L".exe") == 0 || _wcsicmp(ext.c_str(), L".dll") == 0) {
                std::wstring name = entry.path().filename().wstring();

                // TSF DLL is handled separately after extraction — it may be
                // mapped into foreign host processes and cannot be bulk-moved
                // safely alongside the EXE kill path.
                if (_wcsicmp(name.c_str(), kTsfDllFilename) == 0) continue;

                std::wstring destPath = oldVersionDir + L"\\" + name;
                DeleteFileW(destPath.c_str());
                MoveFileW(entry.path().c_str(), destPath.c_str());
            }
        }
```

- [ ] **Step 4: Invoke `HandleTsfDllReplace` after extraction**

Still in `RunUpdateInstaller`, find the copy block (around line 229, after `CopyDirectoryContents(sourceDir, exeDir);`). Add the TSF DLL handling *before* that generic copy so the generic copy does not also try to overwrite the locked live DLL:

Replace:
```cpp
        // 5. Copy new files to exe directory
        CopyDirectoryContents(sourceDir, exeDir);
```
with:
```cpp
        // 5a. Special-case TSF DLL (may be mapped in foreign host processes).
        //     CopyDirectoryContents below would blindly try to overwrite the
        //     live copy and silently fail; instead route via HandleTsfDllReplace
        //     which does the NTFS rename trick + pending-swap fallback.
        {
            std::wstring newTsfDll = sourceDir + L"\\" + kTsfDllFilename;
            if (fs::exists(newTsfDll)) {
                HandleTsfDllReplace(newTsfDll, exeDir, oldVersionDir);
                // Prevent the generic copy from clobbering our decision.
                std::error_code delEc;
                fs::remove(newTsfDll, delEc);
            }
        }

        // 5b. Copy remaining new files to exe directory.
        CopyDirectoryContents(sourceDir, exeDir);
```

- [ ] **Step 5: Build**

```bash
cmake --build build-linux --target NextKeyTests
```

Expected: compiles cleanly (HandleTsfDllReplace is Win32-only but wrapped in the existing Win32 target).

Windows build verification is manual:
```powershell
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1"
```

- [ ] **Step 6: Commit**

```bash
git add src/app/system/UpdateInstaller.h src/app/system/UpdateInstaller.cpp
git commit -m "feat(update): special-case NextKeyTSF.dll during install

Bulk .exe/.dll move step no longer touches the TSF DLL. Post-extract,
route it through HandleTsfDllReplace which attempts optimistic NTFS
rename of the live copy to _old_version/ and falls back to writing a
NextKeyTSF.dll.pending + marker when rename fails."
```

---

### Task 2.2: Deferred swap at EXE startup

**Files:**
- Create: `src/app/system/PendingDllApply.h`
- Create: `src/app/system/PendingDllApply.cpp`
- Modify: `CMakeLists.txt` (add the new source to `NextKeyApp` target)

- [ ] **Step 1: Create the header**

Write `src/app/system/PendingDllApply.h`:

```cpp
// NexusKey - Apply deferred TSF DLL swap at EXE startup
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace NextKey {

/// Result of ApplyPendingDllUpdate() — drives the restart banner.
enum class PendingDllState {
    kNone = 0,               // Nothing pending; normal startup.
    kSwapFailed = 1,         // Pending file exists but live DLL could not be released.
    kSwapDoneNeedsReboot = 2 // New DLL on disk; other processes may still hold the old one.
};

/// Called early in the main-process startup path (before CleanupOldUpdateFiles)
/// in src/app/main.cpp. Inspects `exeDir\\NextKeyTSF.dll.pending`; if present,
/// tries to swap it in for the live copy. Never throws. Safe to call when no
/// pending file exists. Caller publishes the result into SharedState flags
/// (TSF_PENDING_DLL_SWAP / TSF_POST_UPDATE_REBOOT) so subprocess dialogs can
/// observe the state.
PendingDllState ApplyPendingDllUpdate() noexcept;

/// Prompt for reboot and call ExitWindowsEx(EWX_REBOOT|EWX_RESTARTAPPS).
/// Acquires SE_SHUTDOWN_NAME privilege automatically. Shared entry point used
/// by both SettingsDialog (Sciter) and ClassicSettingsDialog + TrayIcon.
/// `owner` is used as the MessageBox parent; may be nullptr.
void RestartWindowsWithPrompt(HWND owner) noexcept;

}  // namespace NextKey
```

- [ ] **Step 2: Create the implementation**

Write `src/app/system/PendingDllApply.cpp`:

```cpp
// NexusKey - Apply deferred TSF DLL swap at EXE startup
// SPDX-License-Identifier: GPL-3.0-only

#include "PendingDllApply.h"
#include "UpdateInstaller.h"  // for kTsfDllFilename

#include <Windows.h>
#include <filesystem>
#include <string>

namespace NextKey {

namespace {

std::wstring GetExeDirW() {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) return L".";
    std::wstring full(buf, len);
    auto pos = full.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? full.substr(0, pos) : L".";
}

std::wstring TimestampSuffix() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t ts[32];
    swprintf_s(ts, L"_%04u%02u%02u_%02u%02u%02u_pending",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return ts;
}

}  // namespace

PendingDllState ApplyPendingDllUpdate() noexcept {
    namespace fs = std::filesystem;

    std::wstring exeDir = GetExeDirW();
    fs::path pending = fs::path(exeDir) / (std::wstring(kTsfDllFilename) + L".pending");
    fs::path live    = fs::path(exeDir) / kTsfDllFilename;
    fs::path marker  = fs::path(exeDir) / L"_pending_dll_update";
    fs::path oldDir  = fs::path(exeDir) / L"_old_version";

    std::error_code ec;

    if (!fs::exists(pending, ec)) {
        // Defensive: clean up orphan marker so UI does not show a phantom banner.
        fs::remove(marker, ec);
        return PendingDllState::kNone;
    }

    fs::create_directories(oldDir, ec);
    fs::path parked = oldDir / (std::wstring(kTsfDllFilename) + TimestampSuffix());

    // Step 1: move live → parked. If the live DLL is still mapped in some host
    // process without FILE_SHARE_DELETE, this fails — defer again.
    fs::rename(live, parked, ec);
    if (ec) {
        return PendingDllState::kSwapFailed;
    }

    // Step 2: move pending → live.
    std::error_code ec2;
    fs::rename(pending, live, ec2);
    if (ec2) {
        // Extremely rare: something else grabbed the live name between the two
        // renames. Put the old copy back so the EXE doesn't launch with no DLL.
        fs::rename(parked, live, ec);
        return PendingDllState::kSwapFailed;
    }

    // Swap succeeded. Clear the marker — banner still shows kSwapDoneNeedsReboot
    // because hosts may hold the parked copy mapped in RAM.
    fs::remove(marker, ec);
    return PendingDllState::kSwapDoneNeedsReboot;
}

void RestartWindowsWithPrompt(HWND owner) noexcept {
    // Prompt is deliberately generic — we reuse the same copy as the banner.
    const wchar_t* msg = L"Restart Windows now to finish the update?";  // caller may swap via S() if localized
    if (MessageBoxW(owner, msg, L"NexusKey",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) {
        return;
    }

    // Acquire SE_SHUTDOWN_NAME privilege (required for ExitWindowsEx).
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(),
                         TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp{};
        if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
        }
        CloseHandle(hToken);
    }

    ExitWindowsEx(EWX_REBOOT | EWX_RESTARTAPPS,
                  SHTDN_REASON_MAJOR_APPLICATION
                  | SHTDN_REASON_MINOR_UPGRADE
                  | SHTDN_REASON_FLAG_PLANNED);
}

}  // namespace NextKey
```

Callers that want localized prompt copy should pass the localized string via `S(S_UPDATE_BANNER_PENDING)` (or add a dedicated `S_UPDATE_BANNER_CONFIRM` string) and use `MessageBoxW` themselves — but the shared helper above is the default. If all three dialogs want localized copy, upgrade the helper to take a `const wchar_t*` message parameter and drop the hard-coded string.

- [ ] **Step 3: Add to build**

Find the `NextKeyApp` target in `CMakeLists.txt`:
```bash
grep -n "add_executable(NextKeyApp\|NextKeyApp\s*$\|UpdateInstaller.cpp" CMakeLists.txt
```

Add `src/app/system/PendingDllApply.cpp` to the same source list that already contains `src/app/system/UpdateInstaller.cpp`.

- [ ] **Step 4: Build**

```bash
# Linux build will skip this file (Win32-only), but the cmake generate step
# should complete without error.
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests
```

Expected: configures and builds NextKeyTests cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/app/system/PendingDllApply.h src/app/system/PendingDllApply.cpp CMakeLists.txt
git commit -m "feat(update): ApplyPendingDllUpdate — swap deferred DLL at startup

Runs before any TSF/SharedState work. If a NextKeyTSF.dll.pending
file exists, renames the live copy to _old_version/ and moves pending
into place. Returns a tri-state the EXE uses to drive the restart
banner."
```

---

### Task 2.3: Wire `ApplyPendingDllUpdate` into main-process startup + publish to SharedState

**Files:**
- Modify: `src/app/main.cpp`

Settings runs as a `--settings` subprocess (line 202). A process-local global is therefore **not** a viable signal channel. The main process calls `ApplyPendingDllUpdate` exactly once and publishes the result into the two new SharedState flag bits; all other surfaces (Settings subprocess, Classic dialog, tray) read those flags.

- [ ] **Step 1: Add include**

At the top of `src/app/main.cpp`, near the other `system/` includes, add:

```cpp
#include "system/PendingDllApply.h"
```

- [ ] **Step 2: Call `ApplyPendingDllUpdate` early in the main-process path**

Do **not** call it at `WinMain` entry (that would also fire inside every `--settings` / `--macro` / `--install-update` subprocess, which each have their own copy of the global and could race with each other).

Instead, insert the call inside the main-process block, immediately before `CleanupOldUpdateFiles` (around `src/app/main.cpp:280-285`). Save the result into a local variable:

```cpp
// Apply any deferred TSF DLL swap before the generic update-file cleanup
// (which removes _old_version/ and would delete the parked copy if the
// order were reversed). Only runs in the main process; subprocess routes
// returned above.
PendingDllState pendingDllState = ApplyPendingDllUpdate();

// Clean up leftover files from a previous update.
// If files were cleaned up, it means we just finished an update.
bool updateJustCompleted = CleanupOldUpdateFiles();
```

- [ ] **Step 3: Publish the result into SharedState after `Create()` succeeds**

Find the existing `g_sharedState.Create()` call (around `src/app/main.cpp:317`). Immediately after the `if (g_sharedState.Create()) { ... g_sharedState.Write(state); }` init block, add:

```cpp
// Publish the startup DLL-swap outcome so the Settings subprocess and the
// tray can render a restart banner. Bits are cleared on reboot (SharedState
// is recreated fresh; InitDefaults zeroes flags).
g_sharedState.SetOrClearFlag(SharedFlags::TSF_PENDING_DLL_SWAP,
    pendingDllState == PendingDllState::kSwapFailed);
g_sharedState.SetOrClearFlag(SharedFlags::TSF_POST_UPDATE_REBOOT,
    pendingDllState == PendingDllState::kSwapDoneNeedsReboot);
// TSF_ABI_MISMATCH is cleared by the DLL next time it starts — we do NOT
// clear it here. If the old DLL is still mapped in a host and set the bit,
// the banner stays visible until reboot (which is exactly what we want).
```

- [ ] **Step 4: Build**

```bash
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --target NextKeyTests
```

Expected: builds cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/app/main.cpp
git commit -m "feat(main): apply deferred DLL swap + publish state via SharedState

Runs in main process only (subprocess routes return before this block).
Publishes outcome into TSF_PENDING_DLL_SWAP / TSF_POST_UPDATE_REBOOT so
Settings subprocess and tray can render a restart banner even though
they cannot observe this process's stack."
```

---

## Phase 3 — Banner UI

Goal: user sees an actionable restart prompt; tray mirrors it.

### Task 3.1: Add i18n strings

**Files:**
- Modify: `src/core/Strings.h` and `src/core/Strings.cpp`
- Modify: `src/app/ui/shared/strings.js`

- [ ] **Step 1: Identify the string-dictionary format**

Run:
```bash
grep -n "S_UPDATE\|UPDATE_BANNER\|S_DIALOG\|S(\"" src/core/Strings.h src/core/Strings.cpp | head -20
```

Observe the enum/key style used by existing `S_*` entries. Match that style exactly for the four new keys.

- [ ] **Step 2: Add C++-side strings**

In `src/core/Strings.h`, add four new enumerators (using whatever naming convention the file uses — likely `S_UPDATE_BANNER_PENDING`, etc.):

```cpp
    S_UPDATE_BANNER_PENDING,     // "Update not finished. Restart Windows to apply the new TSF version."
    S_UPDATE_BANNER_MISMATCH,    // "Some apps still run the old version. Restart Windows to sync."
    S_UPDATE_BANNER_RESTART_NOW, // "Restart now"
    S_UPDATE_BANNER_LATER,       // "Later"
```

In `src/core/Strings.cpp`, add the VN + EN entries in the corresponding string table:

```cpp
    // VN
    { S_UPDATE_BANNER_PENDING,     L"Cập nhật chưa hoàn tất. Khởi động lại Windows để áp dụng phiên bản TSF mới." },
    { S_UPDATE_BANNER_MISMATCH,    L"Một vài ứng dụng đang chạy phiên bản cũ. Khởi động lại Windows để đồng bộ." },
    { S_UPDATE_BANNER_RESTART_NOW, L"Khởi động lại ngay" },
    { S_UPDATE_BANNER_LATER,       L"Để sau" },

    // EN
    { S_UPDATE_BANNER_PENDING,     L"Update not finished. Restart Windows to apply the new TSF version." },
    { S_UPDATE_BANNER_MISMATCH,    L"Some apps still run the old version. Restart Windows to sync." },
    { S_UPDATE_BANNER_RESTART_NOW, L"Restart now" },
    { S_UPDATE_BANNER_LATER,       L"Later" },
```

Place each entry in the correct table (the file is structured per-language; the call site `S()` picks by `GetLanguage()`).

- [ ] **Step 3: Add Sciter-side strings**

In `src/app/ui/shared/strings.js`, locate the language dictionaries (typically `const STRINGS = { vi: { ... }, en: { ... } }`). Add:

```js
// inside vi: {
  "update.banner.pending":     "Cập nhật chưa hoàn tất. Khởi động lại Windows để áp dụng phiên bản TSF mới.",
  "update.banner.mismatch":    "Một vài ứng dụng đang chạy phiên bản cũ. Khởi động lại Windows để đồng bộ.",
  "update.banner.restartNow":  "Khởi động lại ngay",
  "update.banner.later":       "Để sau",

// inside en: {
  "update.banner.pending":     "Update not finished. Restart Windows to apply the new TSF version.",
  "update.banner.mismatch":    "Some apps still run the old version. Restart Windows to sync.",
  "update.banner.restartNow":  "Restart now",
  "update.banner.later":       "Later",
```

- [ ] **Step 4: Build to confirm the enum additions compile**

```bash
cmake --build build-linux --target NextKeyTests
./build-linux/tests/NextKeyTests
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/Strings.h src/core/Strings.cpp src/app/ui/shared/strings.js
git commit -m "i18n: add four strings for the TSF update-restart banner

Strings added on both the C++ (Classic UI / tray menu) and
Sciter (Settings dialog HTML) sides. VN copy uses 'Khởi động lại
Windows'; EN copy uses 'Restart Windows' — explicit about Windows,
not the app, because an app-only restart doesn't unload hosts."
```

---

### Task 3.2: Sciter Settings dialog banner

**Files:**
- Modify: `src/app/ui/settings/settings.html`
- Modify: `src/app/ui/settings/settings.css` (or the theme file used)
- Modify: `src/app/ui/settings/settings.js`
- Modify: `src/app/dialogs/SettingsDialog.cpp`

- [ ] **Step 1: Add banner DOM**

In `src/app/ui/settings/settings.html`, insert a banner element above the main content, inside the top-level layout container:

```html
<div id="update-banner" class="update-banner" style="display:none">
  <span class="update-banner__icon">!</span>
  <span id="update-banner-text" class="update-banner__text"></span>
  <button id="update-banner-restart" class="update-banner__btn-primary"></button>
  <button id="update-banner-later" class="update-banner__btn-ghost"></button>
</div>
```

- [ ] **Step 2: Style the banner**

Append to `src/app/ui/settings/settings.css` (or whichever stylesheet the dialog loads — check `src/app/ui/shared/theme.css` for matching tokens):

```css
.update-banner {
  flow: horizontal;
  padding: 10dip 14dip;
  background: #fff4ce;          /* warning bg, matches Win11 infobar */
  border-bottom: 1dip solid #c8a631;
  color: #5e4a00;
  font-size: 13dip;
}
.update-banner__icon {
  flow: block;
  width: 18dip; height: 18dip;
  border-radius: 9dip;
  background: #c8a631;
  color: white;
  text-align: center;
  line-height: 18dip;
  margin-right: 8dip;
}
.update-banner__text { flow: block; padding-right: 12dip; }
.update-banner__btn-primary,
.update-banner__btn-ghost {
  margin-left: 6dip;
  padding: 4dip 10dip;
  border-radius: 4dip;
  font-size: 12dip;
}
.update-banner__btn-primary {
  background: #c8a631;
  color: white;
  border: none;
}
.update-banner__btn-ghost {
  background: transparent;
  color: #5e4a00;
  border: 1dip solid #c8a631;
}
```

- [ ] **Step 3: Wire the banner in JS**

In `src/app/ui/settings/settings.js`, at the bottom of the existing `ready` / init block:

```js
function refreshUpdateBanner() {
  const state = Window.this.xcall("getUpdateBannerState");  // 0=hide, 1=pending, 2=mismatch
  const banner = document.getElementById("update-banner");
  if (!banner) return;

  if (state === 0) {
    banner.style.display = "none";
    return;
  }
  const msgKey = (state === 1) ? "update.banner.pending" : "update.banner.mismatch";
  banner.style.display = "";
  document.getElementById("update-banner-text").textContent = t(msgKey);
  document.getElementById("update-banner-restart").textContent = t("update.banner.restartNow");
  document.getElementById("update-banner-later").textContent   = t("update.banner.later");
}

document.getElementById("update-banner-restart").onclick = () => {
  Window.this.xcall("requestRestartWindows");
};
document.getElementById("update-banner-later").onclick = () => {
  document.getElementById("update-banner").style.display = "none";
  // Do not persist — banner re-appears on next dialog open (design §4).
};

refreshUpdateBanner();
// Poll: abiMismatch may flip at any time while the dialog is open.
setInterval(refreshUpdateBanner, 2000);
```

(Adjust `t(...)` to whatever the project's i18n-lookup helper is — check `strings.js` for the exported name.)

- [ ] **Step 4: Expose the native callbacks from `SettingsDialog.cpp`**

In `src/app/dialogs/SettingsDialog.cpp`, find the existing `xcall` / native-function registration block (search for `DEF_FUNC_ARGS` or `sciter::script::function`). Add two handlers:

```cpp
// getUpdateBannerState(): returns 0=hide, 1=pending-copy, 2=mismatch-copy.
// Settings runs as a subprocess so we cannot read any main-process global;
// the main EXE publishes the banner state into SharedState flags, and we
// read them here. SettingsDialog owns sharedState_ directly (see
// SettingsDialog.cpp:74, 1214).
int SettingsDialog::getUpdateBannerState() {
    if (!sharedState_.IsConnected()) return 0;
    const uint32_t flags = sharedState_.ReadFlags();
    if (flags & SharedFlags::TSF_PENDING_DLL_SWAP) return 1;
    if (flags & (SharedFlags::TSF_POST_UPDATE_REBOOT | SharedFlags::TSF_ABI_MISMATCH)) return 2;
    return 0;
}

// requestRestartWindows(): defer to the shared helper in PendingDllApply.cpp
// so the Classic dialog and the tray share one implementation.
void SettingsDialog::requestRestartWindows() {
    RestartWindowsWithPrompt(hwnd_);
}
```

Register them in the dialog's Sciter function table (match the existing pattern — `DEF_FUNC_ARGS` or the `on_script_call` dispatcher).

Include `"system/PendingDllApply.h"` (for `RestartWindowsWithPrompt`, added in Task 3.3) at the top of `SettingsDialog.cpp`.

- [ ] **Step 5: Repack Sciter resources**

```bash
extern/sciter/bin/packfolder.exe src/app/ui src/app/resources.cpp -v resources
```

Expected: `src/app/resources.cpp` regenerated.

- [ ] **Step 6: Build (Windows, manual)**

```powershell
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1"
```

Verify that opening Settings with a live `NextKeyTSF.dll.pending` file in the install dir (so the main EXE sets `TSF_PENDING_DLL_SWAP` on startup) shows the banner. With no pending file and no mismatch, banner is hidden.

- [ ] **Step 7: Commit**

```bash
git add src/app/ui/settings/settings.html \
        src/app/ui/settings/settings.css \
        src/app/ui/settings/settings.js \
        src/app/dialogs/SettingsDialog.cpp \
        src/app/resources.cpp
git commit -m "feat(settings): restart banner driven by pending-DLL + ABI flag

Banner hides by default; shows 'pending' copy when the startup swap
failed, 'mismatch' copy when the swap succeeded (hosts still hold
old DLL) or the TSF DLL set TSF_ABI_MISMATCH. Restart button uses
ExitWindowsEx(EWX_REBOOT|EWX_RESTARTAPPS)."
```

---

### Task 3.3: Classic Settings dialog banner

**Files:**
- Modify: `src/app/classic/ClassicSettingsDialog.cpp`

- [ ] **Step 1: Locate the classic dialog's top-of-window render point**

Run:
```bash
grep -n "WM_CREATE\|InitDialog\|CreateWindow\|ClassicTheme" src/app/classic/ClassicSettingsDialog.cpp | head -20
```

Identify the function that creates the root layout and inserts the tab control. The banner must sit above the tabs.

- [ ] **Step 2: Add banner creation**

Add a helper near the top of the file:

```cpp
namespace {
constexpr int kUpdateBannerId       = 9101;
constexpr int kUpdateBannerBtnRestart = 9102;
constexpr int kUpdateBannerBtnLater   = 9103;
constexpr int kUpdateBannerHeight   = 32;
}

// ClassicSettingsDialog owns sharedState_ directly (see :526, :671). Pass it in.
// Banner state is driven entirely by SharedState flags (Classic dialog, like
// Sciter's Settings, runs as a subprocess and cannot observe main's locals).
static void ShowUpdateBanner(HWND hwndDlg, SharedStateManager& sharedState) {
    int state = 0;  // 0=hide, 1=pending copy, 2=mismatch copy
    if (sharedState.IsConnected()) {
        const uint32_t flags = sharedState.ReadFlags();
        if (flags & SharedFlags::TSF_PENDING_DLL_SWAP) state = 1;
        else if (flags & (SharedFlags::TSF_POST_UPDATE_REBOOT | SharedFlags::TSF_ABI_MISMATCH)) state = 2;
    }

    HWND banner = GetDlgItem(hwndDlg, kUpdateBannerId);
    if (state == 0) {
        if (banner) ShowWindow(banner, SW_HIDE);
        return;
    }

    const wchar_t* msg = (state == 1)
        ? S(S_UPDATE_BANNER_PENDING)
        : S(S_UPDATE_BANNER_MISMATCH);

    if (!banner) {
        RECT rc; GetClientRect(hwndDlg, &rc);
        banner = CreateWindowW(L"STATIC", msg,
                               WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_LEFT,
                               0, 0, rc.right, kUpdateBannerHeight,
                               hwndDlg, reinterpret_cast<HMENU>(kUpdateBannerId),
                               GetModuleHandle(nullptr), nullptr);
        CreateWindowW(L"BUTTON", S(S_UPDATE_BANNER_RESTART_NOW),
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      rc.right - 220, 4, 100, 24,
                      hwndDlg, reinterpret_cast<HMENU>(kUpdateBannerBtnRestart),
                      GetModuleHandle(nullptr), nullptr);
        CreateWindowW(L"BUTTON", S(S_UPDATE_BANNER_LATER),
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      rc.right - 110, 4, 100, 24,
                      hwndDlg, reinterpret_cast<HMENU>(kUpdateBannerBtnLater),
                      GetModuleHandle(nullptr), nullptr);
    } else {
        SetWindowTextW(banner, msg);
        ShowWindow(banner, SW_SHOW);
    }
}
```

Call `ShowUpdateBanner(hwndDlg)` from both `WM_INITDIALOG` and a `SetTimer`-driven periodic refresh (every 2000 ms) so the ABI-mismatch bit is observed live.

Adjust the tab-control / content-area rectangle to offset by `kUpdateBannerHeight` when the banner is visible (or always reserve the space — simpler, matches Sciter version).

- [ ] **Step 3: Handle button clicks in `WM_COMMAND`**

In the dialog's `WndProc`:

```cpp
case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case kUpdateBannerBtnRestart:
        // Same ExitWindowsEx flow as Sciter path — refactor into a shared
        // helper in PendingDllApply.cpp if both dialogs need it.
        RestartWindowsWithPrompt(hwndDlg);
        return 0;
    case kUpdateBannerBtnLater:
        ShowWindow(GetDlgItem(hwndDlg, kUpdateBannerId), SW_HIDE);
        ShowWindow(GetDlgItem(hwndDlg, kUpdateBannerBtnRestart), SW_HIDE);
        ShowWindow(GetDlgItem(hwndDlg, kUpdateBannerBtnLater), SW_HIDE);
        return 0;
    // ... existing handlers
    }
    break;
```

`RestartWindowsWithPrompt` is already defined in `src/app/system/PendingDllApply.cpp` (Task 2.2 Step 2). Include `"system/PendingDllApply.h"` at the top of `ClassicSettingsDialog.cpp`.

- [ ] **Step 4: Build (manual Windows)**

```powershell
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKeyClassic --config Debug 2>&1"
```

Smoke: launch Classic build with a stub `NextKeyTSF.dll.pending` present → banner renders above tabs.

- [ ] **Step 5: Commit**

```bash
git add src/app/classic/ClassicSettingsDialog.cpp src/app/system/PendingDllApply.h src/app/system/PendingDllApply.cpp
git commit -m "feat(classic): mirror the Sciter restart banner in classic UI

Classic dialog reserves 32 px at the top for a Win32 STATIC banner +
two BUTTON controls. RestartWindowsWithPrompt() extracted into
PendingDllApply so both Sciter and Classic paths share the ExitWindowsEx
flow and the privilege-acquisition code."
```

---

### Task 3.4: Tray menu "Restart to finish update" item

**Files:**
- Modify: `src/app/system/TrayIcon.cpp`

- [ ] **Step 1: Locate the tray menu build site**

```bash
grep -n "CreatePopupMenu\|AppendMenu\|TrackPopupMenu\|WM_COMMAND" src/app/system/TrayIcon.cpp | head -20
```

Find the function that assembles the tray right-click menu.

- [ ] **Step 2: Give TrayIcon a SharedStateManager pointer**

TrayIcon currently has no SharedState access (verified by `grep -n "SharedState" src/app/system/TrayIcon.*` returning empty). Thread the existing `g_sharedState` pointer from `main.cpp` through.

In `src/app/system/TrayIcon.h`, add to the constructor / init:

```cpp
// Optional: non-owning pointer to the process-wide SharedStateManager.
// Used only to read TSF_ABI_MISMATCH for the restart-menu item.
void SetSharedState(SharedStateManager* mgr) noexcept { sharedState_ = mgr; }
```

and the private member:
```cpp
    SharedStateManager* sharedState_ = nullptr;
```

In `src/app/main.cpp` where the tray icon is constructed (search `TrayIcon tray;` or the `tray_` member init), insert after the tray is built and after `g_sharedState.Create()` has succeeded:

```cpp
tray.SetSharedState(&g_sharedState);
```

(Use the exact variable name from the existing code. If the global pointer is passed around via a different accessor, use that.)

- [ ] **Step 3: Insert the conditional item at the top of the menu**

At the very top of the menu-construction sequence (before "Settings", "Enable Vietnamese mode", etc.):

```cpp
// Reboot banner — shown when ANY of the three update flags is live.
// Tray lives in the main process, which itself set TSF_PENDING_DLL_SWAP /
// TSF_POST_UPDATE_REBOOT in Task 2.3 Step 3. TSF_ABI_MISMATCH is set
// cross-process by the TSF DLL.
bool showRestart = false;
if (sharedState_ && sharedState_->IsConnected()) {
    const uint32_t f = sharedState_->ReadFlags();
    showRestart = (f & (SharedFlags::TSF_PENDING_DLL_SWAP
                       | SharedFlags::TSF_POST_UPDATE_REBOOT
                       | SharedFlags::TSF_ABI_MISMATCH)) != 0;
}
if (showRestart) {
    AppendMenuW(hMenu, MF_STRING, kTrayCmdRestartWindows,
                S(S_UPDATE_BANNER_RESTART_NOW));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
}
```

Declare `kTrayCmdRestartWindows` alongside the file's other `kTrayCmd*` command IDs (pick an unused integer).

- [ ] **Step 4: Handle the command**

In the tray's `WM_COMMAND` dispatcher:

```cpp
case kTrayCmdRestartWindows:
    NextKey::RestartWindowsWithPrompt(hwnd_);
    break;
```

Include `"PendingDllApply.h"` at the top of `TrayIcon.cpp`.

- [ ] **Step 5: Build (manual Windows)**

```powershell
powershell.exe -Command "cd '\\wsl.localhost\Ubuntu-24.04\home\phatmt\code\NexusKey\build'; cmake --build . --target NexusKey --config Debug 2>&1"
```

Smoke: with `.pending` present, right-click tray → top item is "Restart now". Without it, menu has no extra item.

- [ ] **Step 6: Commit**

```bash
git add src/app/system/TrayIcon.h src/app/system/TrayIcon.cpp src/app/main.cpp
git commit -m "feat(tray): surface restart-to-finish-update in tray menu

Conditional on any of TSF_PENDING_DLL_SWAP, TSF_POST_UPDATE_REBOOT,
or TSF_ABI_MISMATCH being live. Shares the ExitWindowsEx helper with
both Settings dialogs — no code duplication."
```

---

## Verification (manual, after all phases)

Run the integration flow documented in the design doc § 5, then archive the result in `docs/update-test-plan.md` (create it if missing):

1. Build v1.0 with `CURRENT_VERSION = 4`, `reserved[1024]`, TSF still compiled but disabled (no `regsvr32`). Install.
2. Temporarily patch local checkout: bump `CURRENT_VERSION = 5`. Build v1.1.
3. With v1.0 installed and `NextKeyTSF.dll` regsvr32'd, open Word and type to force TIP load.
4. Run v1.0 → v1.1 update.
5. Verify in order:
   - `NextKeyTSF.dll` on disk has the v1.1 timestamp.
   - `_old_version/NextKeyTSF.dll_<ts>` exists with the v1.0 timestamp.
   - Word still types (old DLL mapped); it produces English only (passthrough) — indicates the ABI check fired.
   - Live `SharedState.flags` has bit `0x0020` set (use `--diag` route to dump).
   - Opening Settings shows the "mismatch" banner; tray shows "Restart now".
6. Close Word, re-open → Vietnamese typing resumes with v1.1 semantics.
7. Reboot → banner clears, flag re-initialized to 0.

---

## Out of scope

- Enabling TSF in production. Gated on manual verification of the flow above.
- Auto-reboot scheduling. User initiates from the banner.
- Admin/`%ProgramFiles%` install — would need `MOVEFILE_DELAY_UNTIL_REBOOT` and UAC, separate design.
- Detecting which specific hosts still hold the old DLL (would need `CreateToolhelp32Snapshot` + per-process module enum; banner copy intentionally generic to avoid false precision).
