# TODO

## Macro Case-Matching + Multi-word Macros — Follow-ups (2026-04-22)

Landed: issue #98 fix (case-insensitive match for all-lowercase keys, strict for
keys with any uppercase), `VkToMacroChar` Shift-aware via `ToUnicodeEx`, and
multi-word macro keys via phrase-prefix buffer preservation. Rules doc:
`docs/macro-case-rules.md`. Design: `docs/plans/2026-04-22-multiword-macro-design.md`.

### Tech debt surfaced

- [ ] **Extract `ApplyAutoCapsMacro` to pure function** — `src/app/system/HookEngine.cpp:2778-2826`
  Auto-caps transform (expansionAllLower check, allUpper/firstUpper detection,
  `CharUpperBuffW` loop) is inline in `TryExpandMacro` and therefore untestable
  from Linux. Mirror of `src/core/MacroPrefix.h`: extract to `src/core/MacroCase.h`
  with a Linux stub for `CharUpperBuffW` (or inject the case-fold fn) and add
  gtest covering the four-quadrant matrix (stored-case × typed-case) flagged in
  party-mode review.

- [ ] **Zero unit tests on `TryExpandMacro` match logic** — `src/app/system/HookEngine.cpp:2699-2747`
  Two-step find (`find(rawBuffer)` → `find(lowerKey)`), `matchedExact` flag, and
  P1/P2/P3/P4 priority ordering have no coverage. Extract the matching contract
  into a pure `ResolveMacroMatch(buffer, previousComp, trigger, table) ->
  {iterator, matchedExact, matchedViaComposition}` and table-test it on Linux.

- [ ] **Composition-path (P3/P4) auto-caps asymmetry** — `src/app/system/HookEngine.cpp:2732-2745, 2786`
  Auto-caps explicitly gated on `!matchedViaComposition` — typing `CHOOL` via
  Telex composing to `chôl` and matching a stored `chol` macro yields `chôl`,
  not `CHÔL`. Intentional (composition case is engine-driven, not user-typed),
  but users may not understand the asymmetry. Decide: document as a known limit
  in `docs/macro-case-rules.md`, or mirror the auto-caps logic on composition
  matches too.

- [ ] **`\n` escape handling in auto-caps loop is incomplete** — `src/app/system/HookEngine.cpp:2806-2813`
  Loop skips the 2-char `\n` escape when uppercasing. Does not handle `\t`,
  `\\`, or any other escape. Verify what `LoadMacros` actually does with
  escapes at load:
    - if decoded at load → the `\n` skip is dead code; remove it.
    - if passed through verbatim → `\t` under auto-upper corrupts to `\T`.

- [ ] **`VkToMacroChar` syscalls per commit trigger** — `src/app/system/HookEngine.cpp:2851-2888`
  Calls `GetAsyncKeyState ×3`, `GetKeyState`, `MapVirtualKeyW ×2`,
  `GetForegroundWindow`, `GetWindowThreadProcessId`, `GetKeyboardLayout`,
  `ToUnicodeEx` each time. Only runs on commit triggers (~10/sec human typing),
  so cost is negligible — but the foreground HKL could be cached on focus
  change if ever profiled hot. Low priority.

- [ ] **Release-note the case-matching behavior change** — `RELEASE_NOTES.md`
  Pre-fix: uppercase-keyed macros never fired at all (bug). Post-fix: they fire
  but only on exact case. Users who worked around the bug by adding lowercase
  duplicates will now see both entries match. Note must call this out with the
  migration example from `docs/macro-case-rules.md`.

---

## Detach Fork
- [x] Detach fork: repo `phatMT97/NexusKey` is forked from `tuyenvm/OpenKey`. Submitted GitHub Support ticket (2026-04-05).

---

## Sub-dialog Instant Apply — Fixed (2026-04-22)

User feedback (v2.1.21): adding an app to TSF list required closing Settings before
the new entry took effect; target app stayed in Hook mode until its next focus gain.

### Root cause
`SignalConfigChange()` only bumps `configGeneration` in SharedState; main EXE's
`HookEngine::QuickSyncFromSharedState()` is called from `ProcessKeyDown` /
`OnFocusChanged` only — no periodic poll (the 100 ms `ConfigPollTimerProc` was
removed in commit `fcfd9c4` when the Named-Event → generation migration landed).
While Settings owns foreground, the target app can't fire `OnFocusChanged` → reload
is deferred until Settings closes.

### Fix — landed
Added `WM_NEXUSKEY_HOOK_RELOAD` cross-process ping from `SignalConfigChange()` to
the main EXE tray window. Tray forwards to a `SetHookReloadCallback` handler wired
to a new public `HookEngine::SyncConfigFromSharedState()` (thin wrapper around the
private `QuickSyncFromSharedState` so we don't touch the auto-reset Named Event,
which is reserved for the TSF DLL — consuming it in the main EXE would steal the
signal from `EngineController::CheckConfigEvent`).

Applies to every sub-dialog that calls `SignalConfigChange`: TsfApps, ExcludedApps,
AppOverrides, MacroTable, SpellExclusions, ConvertTool.

### Tech-debt items surfaced
- [ ] **`FindWindowW(L"NexusKeyTrayClass") + PostMessageW` pattern duplicated**
  Now in `AppHelpers.h::SignalConfigChange`, `SettingsDialog.cpp:548,698,1286`,
  `ClassicSettingsDialog.cpp:813,991,998,1033`. Candidate for a
  `PostToTrayWindow(UINT msg, WPARAM = 0, LPARAM = 0)` helper in `AppHelpers.h`.
  Low priority — consistent with existing pattern.

- [ ] **`HookEngine::CheckConfigEvent()` has no callers in main EXE**
  TSF DLL uses its own `EngineController::CheckConfigEvent` (separate class).
  Marked `// Legacy path — kept for TSF DLL compatibility` but that comment is
  misleading: the TSF DLL never called the HookEngine version. Candidate for
  deletion along with `configEvent_` member + `Initialize()` call at
  `HookEngine.cpp:129`. Out of scope for this fix.

---

## Auto-caps + TSF Apps Feedback — Follow-ups (2026-04-21)

User feedback batch (v2.1.19 Hybrid-TSF testing). Fixed items landed in commits
`7548dea`, `e53176b`, `a3f00c6`, `f89ea4d`. Remaining items below.

### Unfinished from user feedback

- [ ] **Windows Search cannot type Vietnamese** — `searchapp.exe` / `SearchHost.exe`
  UWP AppContainer rejects third-party TIP load → TSF DLL never instantiated.
  User workaround (add to TSF list) DID NOT WORK (confirmed on v2.1.21).
  Observation: when Windows Search gains focus from Edge, Input Indicator
  auto-switches from "NexusKey Vietnamese IME" to "English (US) US Keyboard"
  — indicates Windows is forcibly changing the active IME profile, not just
  blocking our TIP. Screenshot evidence in feedback 2026-04-21.
  **Actual fix path**: add both exes to a TSF-EXCLUSION list ("force Hook
  for these") so Hook handles them. Confirm `WH_KEYBOARD_LL` reaches UWP
  AppContainer. Investigate whether IME profile auto-switch also suppresses
  hook delivery. Affects: Start menu, Settings app search, Win+S.

- [ ] **Arrow-left revive drops auto-cap state** — REPRODUCIBLE 100% in Edge (2026-04-21 retest)
  Steps: type `wqewqe` + space → displays `Wqewqe ` (auto-cap fired on
  first char). Arrow-left once (caret between 'e' and ' '). Type `a` +
  space → final text `wqewqea` (first `W` demoted to lowercase).
  Confirmed on v2.1.19 AND v2.1.21 in Edge native search, GitHub Issue box,
  Google Keep, Facebook. Config: simple_telex, auto_caps=true, tsf_apps
  includes msedge.exe.
  **Code trace expectation**: `InspectPrecedingTextEditSession` reads
  "Wqewqe", `tempEngine->SeedFromText` seeds states with isUpper=true for
  'W'. English-classification TBD — if `HardEnglish` → revive SKIPPED → 'a'
  starts fresh composition, W untouched → would show `Wqewqea`. If revive
  FIRES → SeedFromText + PushChar('a') → Peek composes "Wqewqea" with W
  upper. Either path preserves W — so observed lowercase-demotion is from
  a third code path not yet identified.
  **Hypothesis**: revive DOES fire, but `Peek()` output at
  `CompositionEditSession.h:444` emits lowercase; OR `SetCompositionText`
  writes a different string than composed.
  **Action**: instrument `EngineController.cpp:283` (revive log),
  `CompositionEditSession.h:444` (composed log), ask user to capture with
  DebugView++ and report the composed string.

- [ ] **Arrow-left revive breaks Vietnamese word (strips diacritics)** —
  REPRODUCIBLE 100% in Edge + Word 2024 LTSC (2026-04-21 retest)
  Steps: type `bưởi` + space → `Bưởi `. Arrow-left. Type any letter
  (`a`/`A`/`b`/`B`) → text becomes `buoi` (all caps + horn + tone LOST,
  typed character also missing or misplaced).
  Confirmed on v2.1.19 AND v2.1.21. Config: simple_telex, auto_caps=true.
  **Critical observation**: the OUTPUT "buoi" equals `SeedFromText`'s
  synthetic `rawInput_` (base letters only — see TypingEngine.cpp:1319
  which pushes only `base` to rawInput_, no tone/mod keystrokes). This
  strongly suggests an auto-restore path fires that returns
  `std::wstring(rawInput_.begin(), rawInput_.end())` — matches
  TypingEngine.cpp:1266 in `Commit()`.
  **But**: `ReviveAndTypeEditSession` uses `Peek()` not `Commit()`, so
  auto-restore shouldn't apply. Unless some other path reads rawInput_
  under invalid/HardEnglish state, or `SetCompositionText` is being fed
  raw characters instead of composed Peek output.
  **Action**: same diagnostic as item above. Add log at
  `CompositionEditSession.h:444`: `TSF_LOG(L"Revive composed='%ls'
  rawInput='%ls'", composed, raw)`. Repro in Edge and attach log.

- [ ] **Word-boundary protection test matrix**
  User asks whether gluing two words (no space) corrupts the earlier word in
  either Hook or TSF mode. Architecturally: Hook doesn't touch committed text;
  TSF revive only seeds the trailing word. No regression expected, but no
  explicit test. Add scenarios: `xinchao` + backspace-into-word + retype,
  `bưởichuối` edit sequences, commit-trigger behavior on punctuation glue.
  Location: add to `tests/` once revive paths are testable from Linux (see
  "Extract ShouldAutoCap to pure function" below).

### Tech debt surfaced during code review

- [ ] **Extract shared `Import/ExportStringList` helpers** — `src/app/dialogs/DialogUtils.h`
  4 dialogs now duplicate ~60 lines each: `ExcludedAppsDialog`,
  `MacroTableDialog`, `SpellExclusionsDialog`, `TsfAppsDialog`. Differences
  are: window title, default filename, file header comment, and line
  transform. A templated helper with `std::function<std::wstring(std::string)>`
  transform + 3 string params would unify them and prevent future drift.
  Touching all 4 dialogs in one refactor PR — out of scope for feature work.

- [ ] **i18n the import-confirm MessageBox** — `StringId::IMPORT_KEEP_EXISTING`
  All 4 list dialogs hardcode the Vietnamese UTF-16 escape sequence
  `L"Bạn có muốn giữ lại danh sách hiện tại không?"` + per-dialog title.
  Should go through `S(StringId::...)` like other user-facing strings. Pairs
  with the helper extraction above.

- [ ] **Action-string constants** — 4 dialogs
  `handle_event` compares raw wide strings (`L"import"`, `L"export"`,
  `L"close"`, `L"add-manual"`, `L"add-current"`, `L"delete"`,
  `L"get-running-apps"`). Define `namespace DialogActions { inline constexpr
  const wchar_t* IMPORT = L"import"; ... }` in a shared header so typos become
  compile errors. Pairs with the helper extraction above.

- [ ] **Extract `ShouldAutoCap` logic to pure function** — `src/tsf/CompositionEditSession.h:312`
  Auto-cap decision (skip whitespace + check punct/newline + `skippedWhitespace`
  gate) is currently inside `InspectPrecedingTextEditSession::DoEditSession`
  which needs TSF APIs → untestable on Linux. Extract to free
  `bool ComputeShouldAutoCap(const wchar_t* buf, size_t len) noexcept` and
  call from the edit session. Lets Linux tests cover the 3rd auto-cap site
  (currently only the Hook-anchor `DeriveAnchorFromPreceding` has tests).

---

## Hotkey Refactor — Deferred (2026-04-20)

Reviewed deferred items from the HotkeyManager multi-slot refactor (commits pending). All non-blocking; fixed items already landed in the refactor.

- [x] **Extract `WireHotkeys` helper** — `src/app/system/HotkeyWiring.{h,cpp}`
  Extracted ~25 duplicate lines from main.cpp + main_lite.cpp. Lambda factory approach: captures refs to globals for config reload callback.

- [x] **`HotkeyConfig::ModifiersMatch(ctrl, shift, alt, win)` helper** — `src/core/config/TypingConfig.h`
  Added method, used by `matchCombo` and `matchModifierOnlyRelease` lambdas in HotkeyManager.cpp.

- [ ] **`ReloadFromToml` parses 7 TOMLs per config bump** — `src/app/system/HookEngine.cpp:368-472`
  Call graph on `configGeneration` bump:
  ```
  ReloadFromToml()
  ├─ LoadOrDefault()           → toml::parse_file  ①
  ├─ LoadMacros()              → toml::parse_file  ②
  ├─ LoadAppOverrides()        → toml::parse_file  ③
  ├─ LoadAllExcludedApps()     → toml::parse_file  ④
  ├─ LoadTsfApps()             → toml::parse_file  ⑤
  └─ configReloadCallback_()   [HotkeyWiring.cpp:28-37]
     ├─ LoadConvertConfigOrDefault()  → toml::parse_file  ⑥
     └─ LoadHotkeyConfigOrDefault()   → toml::parse_file  ⑦
  ```
  **7× parse of same file** per Settings Save. Cold cache ~35-100ms, warm cache <5ms.
  User-paced trigger → imperceptible. **Low priority** — profile first if perceived lag.

- [ ] **`ScopedForegroundRestore` RAII helper** — `src/app/system/TrayIcon.cpp:379-396`
  `prevFg = GetForegroundWindow()` + `SetForegroundWindow(prevFg)` pattern. Only 1 call site today; `ClassicDialogUtils.h:157` and `WindowPickerDialog.cpp:88` do similar one-shot restores but not the full save-and-restore pair. Not enough duplication to justify a helper yet — revisit if a 3rd call site appears.

- [ ] **Slot removal API + `kInvalidSlotId` sentinel** — `src/app/system/HotkeyManager.h:26-42`
  `using SlotId = size_t;` with default `0` means slot 0 is ambiguous (valid id vs. unset). Today's usage is fine (all slots registered at startup, never removed), but if slot removal is ever added, introduce `static constexpr SlotId kInvalid = SIZE_MAX;` and have `UpdateHotkey` return a bool or check against the sentinel. Low priority until a remove API is actually needed.

---

## TSF Readonly Context — Future Phases (2026-04-19)

Phase 1 shipped: auto-cap via `HookContextAnchor` (commits `89d1add`..`b0bbb09`).
Design doc: `docs/plans/2026-04-19-tsf-readonly-context-phase1-design.md`.
Infra (`anchor.currentSyllable[16]`, seqlock helpers) already in place; phase 1
does not read the syllable field.

### Phase 2 — Cross-boundary tone

- [ ] **Hook uses `anchor.currentSyllable` as prefix when buffer is empty**
  Use case: document has `"hoa"` (paste, or user typed then moved cursor back to
  end). User hits `f`. Today Hook buffer is empty → `f` typed literally →
  `"hoaf"`. TSF full-TIP handles this via `EngineController::TryReviveOnType`
  (`src/tsf/EngineController.cpp:105-141`) — read preceding word, seed engine,
  commit via backspace + replace.

  Phase 2 = port `TryReviveOnType` to Hook using the anchor:
  1. In `HookEngine::HandleAlphaKey`, gate on `engine_->Count() == 0` AND
     anchor snapshot has `syllableLen > 0` AND `!isWordStart` AND `isAvailable`.
  2. `IInputEngine::SeedFromText(anchor.currentSyllable, syllableLen)` — API
     already exists (`TryReviveOnType` calls it).
  3. Push current char into engine as normal.
  4. On commit: `SendBackspaces(syllableLen)` + `SendCharEvents(newText)`.

  Risks / gates:
  - **Stale anchor** → backspaces delete wrong chars. Mitigate: re-read anchor
    snapshot immediately before committing and verify `generation` unchanged
    since the read that triggered revive. Abort if changed.
  - **English-word gate**: `TryReviveOnType` uses `IsEnglishWord()` on a
    throwaway engine to skip English words. Hook must do the same or it'll
    revive `"hello" + f → "helló"`.
  - **Commit-char mismatch**: `TryReviveOnType` also handles English protection
    (`ALLOW_ENGLISH_BYPASS`). Hook path needs parity.

  Files: `src/app/system/HookEngine.cpp` (new helper `TryReviveFromAnchor`),
  `tests/HookEngine*` (new tests for paste+tone, click+tone scenarios).

### Phase 3 — Word continuation mid-word click

- [ ] **Hook seeds engine from anchor when user types inside an existing word**
  Use case: `"hu|ong"` with caret between `u` and `o`. User hits `w` expecting
  `"hương"`. Hook today sees empty buffer → literal `w` → `"huwong"`.

  Phase 3 = detect mid-word typing via `anchor.syllableLen > 0 && !isWordStart`
  (same gate as phase 2, but NOT gated on `engine_->Count() == 0` — rather, we
  seed on entry and continue building). Overlaps heavily with phase 2 — likely
  merges into one code path with different commit strategies based on whether
  the cursor is at end of word vs middle.

  Additional risk: detecting cursor position inside the word. TSF gives us
  chars before cursor, not chars after. Hook can't easily see what's after the
  caret without another sync read (expensive + async-locked in TSF).
  Mitigation: phase 3 may require anchor v2 that includes a few chars AFTER
  cursor too. Design pending.

### Shared infra items

- [ ] **Opportunistic prime on `OnSetFocus`** — `src/tsf/ReadonlyContextProvider.cpp:228`
  Currently we wait for first `OnEndEdit` to push an anchor after focus gain.
  First keystroke in a newly-focused app therefore uses `isAvailable=0`
  (cleared by previous focus-out) and falls back to keystroke state. Acceptable
  for phase 1 but phase 2+ will miss revive on the first key after app switch.
  Fix: request a sync read session on focus gain to prime the anchor.

- [ ] **Logging instrumentation** — `src/tsf/ReadonlyContextProvider.cpp:326`
  TODO comment in-place. Wire up once user-facing log infra lands.

---

## Review (2026-04-19) — TSF revive + auto-cap

Review after commits `7146077`..`feca899` (revive composition, auto-cap, punct
commit-with-char, ref-count fix).

### PERF

- [x] **Combine read sessions for revive + auto-cap** — `src/tsf/CompositionEditSession.h`, `src/tsf/EngineController.cpp`
  Fixed: `InspectPrecedingTextEditSession` reads text once, extracts word + auto-cap decision.
  Auto-cap path reduced from 3 → 2 sessions. Design: `docs/plans/2026-04-20-combine-read-sessions-design.md`.

### STYLE

- [x] `src/tsf/CompositionEditSession.h:117` — `WCHAR buf[MAX_CHARS]` — stale (64 chars = 128 bytes is fine)
- [x] `src/tsf/EngineController.h:116-124` — docstring repetition — stale (no duplicate, only inline comments)

### Punted on TSF idiomatic rewrite

Separate doc-state cache via `ITfTextEditSink::OnEndEdit` (avoid per-keystroke
sync edit sessions) considered and skipped: current sync-read pattern IS
TSF-supported. Not a "trick". Reconsider if per-keystroke latency becomes a
real complaint on slower hosts.

---

## Deep Review (2026-04-13)

Full-source review covering engine, config/IPC, TSF, HookEngine, dialogs, Classic UI, and CMake.

### BUG — Must fix

- [x] **HookEngine fast-path missing `dwExtraInfo`** — `HookEngine.cpp:2107-2141`
  Stack-allocated `INPUT` buffers in `ReplaceComposition` don't set `dwExtraInfo = NEXUSKEY_EXTRA_INFO`. Hook callback at line 509 checks this field to recognize synthetic events. Without it, `synthEventsPending_` increments but never decrements → leaks upward → 500ms watchdog fires repeatedly. Affects all Unicode-mode typing in standard Win32 apps.
  **Fix**: Add `input.ki.dwExtraInfo = NEXUSKEY_EXTRA_INFO;` to both `appendUnicode` and `appendVk` lambdas. Compare with heap-allocated helpers `AppendUnicodeEvent` (line 1363) and `AppendVkEvent` (line 1375) which already set it correctly.

- [x] **SmartSwitchManager cross-process race** — `SmartSwitchManager.cpp:93-117, 135-151`
  `SetAppMode()` writes `hash`, `vietnamese`, `count` with plain stores — no seqlock. Concurrent `GetAppMode()` reader can see partially-written entry (hash updated but vietnamese stale, or count incremented before entry populated). `LoadFromMap()` sets `count=0` before writing entries → reader sees empty table mid-reload. `SharedStateManager` uses proper seqlock; SmartSwitchManager does not.
  **Fix**: Add seqlock protocol (reader checks epoch before/after, retry on mismatch) — same pattern as `SharedStateManager::Read()`. OR write entry data before incrementing `count` with a write barrier.

- [x] **SharedStateManager `Write()` missing `reserved[]` copy** — `SharedStateManager.cpp:201-224`
  Field-by-field copy skips the `reserved[21]` byte array (SharedState.h:90). Currently zeros, but if a future version stores data in reserved and forgets to update `Write()`, data silently dropped.
  **Fix**: Add `memcpy(p->reserved, state.reserved, sizeof(state.reserved));` after the last field copy.

- [x] **CompositionManager `pContext_` stored without AddRef** — `CompositionManager.cpp:94`
  `pContext_ = pContext` without `pContext->AddRef()`. Used later in `MoveCaretToEnd()`, `ApplyDisplayAttribute()`, `EndComposition()`. If TSF releases context before these calls → dangling pointer. Currently safe (same edit session scope) but violates COM contract.
  **Fix**: `pContext->AddRef()` in `StartComposition`, `pContext_->Release()` in `EndComposition` and `TerminateComposition`.

- [x] **TextService `Activate()` leaks on failure** — `TextService.cpp:76-79`
  When `keyEventSink_->Advise()` fails, returns `E_FAIL` without releasing `pThreadMgr_` (AddRef'd at line 62), `pCategoryMgr_`, or resetting `engineController_`. TSF may not call `Deactivate()` after failed Activate.
  **Fix**: Add cleanup block before `return E_FAIL`: release pThreadMgr_, pCategoryMgr_, reset engineController_.

- [x] **TextService `CoCreateInstance` unchecked** — `TextService.cpp:66-67`
  HRESULT silently discarded. If fails → `pCategoryMgr_` null → no composition underline, but typing works.
  **Fix**: Add `if (FAILED(hr)) { TSF_LOG("CategoryMgr creation failed"); }` — don't abort, just log.

- [x] **LanguageBarButton `TrackPopupMenuEx` null hwnd** — `LanguageBarButton.cpp:156`
  Uses `GetFocus()` which can return NULL (no focused window) → `TrackPopupMenuEx` fails silently, menu won't show.
  **Fix**: `HWND hwnd = GetFocus(); if (!hwnd) hwnd = GetForegroundWindow(); if (!hwnd) hwnd = GetDesktopWindow();`

- [x] **ConvertToolDialog `std::stoi` unguarded** — `ConvertToolDialog.cpp:193`
  `getDropdownValue()` calls `std::stoi()` without try/catch. Malformed non-numeric string from Sciter JS → `std::invalid_argument` → crash subprocess.
  **Fix**: Wrap in `try { return std::stoi(s); } catch (...) { return defaultVal; }`.

- [x] **SciterHelper `SetWindowLong` vs `SetWindowLongPtr`** — `SciterHelper.cpp:33`
  Uses `SetWindowLong`/`GetWindowLong` instead of 64-bit correct `SetWindowLongPtrW`/`GetWindowLongPtrW` for `GWL_EXSTYLE`. Currently no crash (EXSTYLE fits 32-bit) but officially wrong per MSDN, flagged by static analyzers.
  **Fix**: Replace with `SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED)`.

- [x] **WindowPicker system cursor not restored on crash** — `WindowPickerDialog.cpp:66-76` + `ClassicDialogUtils.h:143-146`
  Fixed: Added `InstallCursorCrashHandler()` in `AppHelpers.h` using `SetUnhandledExceptionFilter`.
  Called at startup in both `main.cpp` and `main_lite.cpp`. Restores system cursors on crash.

- [x] **main_lite.cpp settings thread missing COM init** — `main_lite.cpp:85-113`
  Settings dialog runs on detached `std::thread` without `CoInitializeEx`/`OleInitialize`. Subdialogs (ConvertTool) use clipboard via `OpenClipboard`, ChooseColor dialog needs OLE. Operations may fail silently.
  **Fix**: Add `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` at start of thread lambda, `CoUninitialize()` before return.

### SECURITY

- [x] **UpdateInstaller ZIP path traversal** — `UpdateInstaller.cpp:132-282`
  `CopyDirectoryContents` uses `fs::relative()` to compute destination paths. Crafted ZIP with `../../` entries could write files outside target directory. Mitigated by SHA-256 hash verification (only legit GitHub ZIPs pass), but lacks defense-in-depth.
  **Fix**: After computing `relativePath`, reject if it contains `..`: `if (relativePath.string().find("..") != std::string::npos) continue;`. Also see existing item below about validate-ZIP-first.

### PERF — Hot path optimizations

- [x] **`IsSpellExcluded` heap alloc per keystroke** — `EngineHelpers.h:76-97`
  Allocates `std::wstring` via `reserve()` + `+=` on every `PushChar()` call. Vietnamese words max ~8 chars.
  **Fix**: Replace with stack `wchar_t buf[16]` + manual length tracking, same pattern as `BuildExclusionBuf` already uses in the same file. Eliminates heap allocation from the per-keystroke hot path.

- [x] **`composeBuf_` return by value defeats optimization** — `TelexEngine.cpp:1041` + `VniEngine.cpp:391`
  `Peek()` returns `composeBuf_` by value → copies string every call. Comment says "avoids heap alloc per Peek" but return-by-value negates this. Buffer capacity reuse only helps internally.
  **Fix**: Change `IInputEngine::Peek()` return type to `const std::wstring&`. Both engines return `composeBuf_` by const-ref. Callers already use the result as temporary. **Note**: Interface change — update IInputEngine.h, TelexEngine, VniEngine, and all callers (EngineController, HookEngine).

- [x] **`IsScintillaApp()` syscalls per keystroke** — `EngineController.cpp:320-346`
  Calls `GetForegroundWindow()` + `GetClassNameW()` + `GetFocus()` every time Space is pressed during composition. Result only changes on focus change.
  **Fix**: Cache the Scintilla detection result in `RefreshFlags()` (called on focus/context change). Add `bool isScintillaApp_` member, check it in `WantKey()`.

- [x] **`ScaleHelper::getDpiScale()` resolves `GetProcAddress` every call** — `ScaleHelper.h:39-40`
  `GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem")` called on every DPI query. Called multiple times during window creation.
  **Fix**: Add `static` before `auto pfn = ...` to cache the function pointer.

- [x] **ClassicIconColorDialog font created/destroyed every WM_PAINT** — `ClassicIconColorDialog.cpp:170-176`
  `CreateFontW()` + `DeleteObject()` inside paint lambda that runs twice per paint cycle.
  **Fix**: Create the preview font once in `WM_INITDIALOG` or as a class member. Destroy in `WM_DESTROY`.

- [x] **HookEngine `Sleep()` in hook callback path** — `HookEngine.cpp`
  Fixed: Reduced Sleep delays ~40%. Electron: 10→6ms, Console: 8→5ms, cap: 20→12ms, clipboard: 15→8ms.
  Still blocking but significantly lower latency for Electron/Console apps.

### SMELL — Code quality

- [x] **SmartSwitchManager `OpenReadWrite()` no existing handle check** — `SmartSwitchManager.cpp:67-88`
  Calling `OpenReadWrite()` twice leaks the first HANDLE + mapping. SharedStateManager properly checks/cleans existing handles.
  **Fix**: Add `if (pImpl_->hMapping) { ... cleanup ... }` at start, same pattern as SharedStateManager.

- [x] **ConfigManager `LoadAllExcludedApps()` no entry count limit** — `ConfigManager.cpp:362-389`
  `LoadMacros()` and `LoadAppOverrides()` enforce max entry limits, but excluded/english/tsf app lists do not. Crafted TOML with millions of entries → memory exhaustion.
  **Fix**: Add `if (entries.size() >= kMaxAppEntries) break;` with a reasonable constant (e.g., 1000).

- [x] **Classic dialogs inconsistent config signaling** — 4 dialogs vs SpellExclusions
  ExcludedApps, MacroTable, AppOverrides, ConvertTool create local `ConfigEvent` + `Signal()` (skips `configGeneration` bump). SpellExclusions uses `SignalConfigChange()` from AppHelpers.h (bumps generation). HookEngine checks `configGeneration` for fast-path reload.
  **Fix**: All Classic subdialogs should use `SignalConfigChange()` from AppHelpers.h consistently. Or inline the same 2-step pattern: bump SharedState generation + signal event.

- [x] **SettingsDialog manual `BuildBodyClasses()` duplication** — `SettingsDialog.cpp:131-139, 373-380`
  Constructor and WM_SETTINGCHANGE handler manually build CSS class string (`dark`, `win10`). `DarkModeHelper::BuildBodyClasses()` exists and is already used in SciterSubDialog.
  **Fix**: Replace both inline blocks with `DarkModeHelper::BuildBodyClasses(dark)`.

- [x] **Dead code: `kTriphthongs[]` array** — `VietnameseTables.h:150-158`
  `kTriphthongs[]` and `kTriphthongCount` defined but never referenced. `IsTriphthong()` uses hardcoded comparisons instead.
  **Fix**: Delete the array and count constant.

- [x] **Dead code: `SwitchInputMethod`** — `EngineController.cpp:303-318`
  Public method never called anywhere. Also has a bug (#18 in TSF review): commits via engine but not via TSF, discarding user's text. Dead code with a bug in it.
  **Fix**: Delete the method declaration and implementation.

- [x] **Dead include: `SecurityHelpers.h`** — `SharedStateManager.cpp:5`
  Included but `MakeCreatorOnlySecurityAttributes()` never called (commented out, doesn't work for non-container objects).
  **Fix**: Remove `#include "SecurityHelpers.h"`.

- [x] **`ConfigEvent.cpp` uses `OutputDebugStringW` directly** — `ConfigEvent.cpp:43,49,51,60`
  Should use `NEXTKEY_LOG()` which compiles out in Release. Current code outputs debug strings in production builds.
  **Fix**: Replace `OutputDebugStringW(...)` with `NEXTKEY_LOG(...)`.

- [x] **`SpellChecker::Validate()` noexcept mismatch** — `SpellChecker.cpp:775`
  Public `Validate()` declared `noexcept`, calls `ValidateImpl()` which is NOT noexcept. If ValidateImpl ever throws → `std::terminate`.
  **Fix**: Add `noexcept` to `ValidateImpl()` declaration and definition.

- [x] **`SystemConfig::englishUI` redundant field** — `SystemConfig.h`
  Fixed: Removed `englishUI` field, added `IsEnglishUI()` method. Updated `SettingMetadata.h` to use `language` field for offset-based UI binding.

- [x] **`Debug.h` buffer overflow behavior** — `Debug.h:23`
  Comment says "Messages longer than 1024 chars are silently truncated" but `vswprintf_s` calls invalid parameter handler (may crash) on overflow. Should use `_vsnwprintf_s` which actually truncates.
  **Fix**: Replace `vswprintf_s(buffer, format, args)` with `_vsnwprintf_s(buffer, 1024, _TRUNCATE, format, args)`.

- [x] **HookEngine redundant SharedStateManager** — `HookEngine.cpp:463-475`
  `ReloadFromToml` creates a new stack `SharedStateManager`, opens it, reads — while `sharedStatePtr_` already exists.
  **Fix**: Use `sharedStatePtr_` directly instead of creating a new instance.

- [x] **SciterSubDialog dead container code** — `SciterSubDialog.cpp:87-93`
  Finds `.container` element, checks validity, does nothing. Comment explains why but code is noise.
  **Fix**: Delete the 6-line block.

### STYLE — Minor cleanup

- [x] `VietnameseTables.h:175,203` — `ToUpperVietnamese`/`ToLowerVietnamese` missing `noexcept`
- [x] `VniEngine.cpp:71` — `Vni::CharState::IsVowel()` should be `constexpr` inline in header (Telex version is)
- [x] `SettingsDialog.cpp:38` — `#define TIMER_RESIZE_WINDOW` should be `static constexpr UINT_PTR` (inconsistent with `TIMER_DEFERRED_SAVE`)
- [x] `ClassicSettingsDialog.h:118` — Add `static_assert(kSettingsCount <= kMaxControls)` to catch overflow at compile time
- [x] `ClassicTheme.cpp:19-25` — Duplicate `IsWindows11OrGreater()` — already in `DarkModeHelper.h` which is included
- [ ] Multi-monitor: 7 Classic dialogs + SettingsDialog + SciterSubDialog use `SM_CXSCREEN` — ignores multi-monitor. Use `MonitorFromWindow` + `GetMonitorInfo` for proper centering. Low priority — works on primary monitor.
- [ ] `TSF_LOG` (Define.h:14-25) outputs 3 separate `OutputDebugStringW` calls per log line — non-atomic, threads can interleave. Concat into single buffer.

---

## Earlier Findings (2026-04-11)

### Actual Bugs (Low severity)
- [x] `SettingsDialog.cpp` — TSF registration MessageBox strings now use `S(StringId::TSF_REGISTER_SUCCESS)` etc.
- [x] `TextService.cpp:66-67` — Merged into deep review BUG list above.
- [x] `SettingsDialog.cpp:787` — Reset settings button — won't fix (user can delete config file)
- [x] `SettingsDialog.cpp:800` — Open log folder button — won't fix (user can navigate manually)
- [x] `ExcludedAppsDialog.cpp` — `MSG_CANNOT_EXCLUDE_SELF` now uses `S(StringId::EXCLUDED_CANNOT_SELF)`.

### Defensive Improvements (nice-to-have)
- [x] `UpdateInstaller.cpp:140-170` — Merged into deep review SECURITY list above (path traversal + validate-ZIP-first).
- [x] `UpdateSecurity.cpp:223` — Predictable temp filename — won't fix (update is user-paced, race impossible)
- [ ] `SettingsDialog.cpp:74-75` — IPC handle errors silently discarded with `(void)`. Add logging on failure.

### Test Coverage Gaps
- [x] Corrupted TOML recovery tests — won't fix (user deletes config, app recreates)
- [x] Unicode surrogate pairs tests — won't fix (no real use case)
- [x] Commit `tests/TelexDictionaryTest.cpp` — already committed (d38ba60), 8 test suites, all passing
