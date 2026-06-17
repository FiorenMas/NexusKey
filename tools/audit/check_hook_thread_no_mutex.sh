#!/usr/bin/env bash
#
# Sprint 1 D7 — Phase B compliance gate.
#
# Verifies three guarantees at the source level:
#   1. The hook-thread `lock_guard<recursive_mutex>` regression-trap lines
#      from D4 SPIKE are still commented — ≥2 in HookEngine.cpp
#      (LowLevelKeyboardProc + LowLevelMouseProc). Wave 3 PR 3.2 moved
#      WinEventProc into FocusOwner.cpp so the threshold dropped from 3 → 2.
#   2. No uncommented `stateMutex_` reference inside any hook-callback entry
#      function body (`LowLevelKeyboardProc`, `WinEventProc`,
#      `LowLevelMouseProc`, `RawInputWndProc`). WinEventProc now SKIPs in
#      HookEngine.cpp; FocusOwner has no stateMutex_, so the invariant
#      holds vacuously there.
#   3. All migrated atomic fields (D5 + D5.1 + D5.2 + D6) use `.load()` /
#      `.store()` — no plain assignment or read of these fields. The atomic
#      RCU `config_` field also obeys this rule.
#
# Run from repo root:
#   bash tools/audit/check_hook_thread_no_mutex.sh
#
# Exit code 0 = pass, non-zero = fail.
#
# Notes:
# * This is a grep-based heuristic, not a full call-graph analysis. It catches
#   the regression patterns that matter (someone adds a `stateMutex_` lock to
#   a hook callback, or reverts an atomic field to plain assignment), at the
#   cost of accepting some over-approximation in comments / format strings.
# * If a check produces a false positive, prefer tightening this script over
#   weakening the source-level invariant.
#
# Reference: docs/plans/sprint-1-single-owner-refactor.md §B D7.

set -e

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo_root"

CPP="src/app/system/HookEngine.cpp"
errors=0

if [ ! -f "$CPP" ]; then
    echo "ERROR: $CPP not found (run from repo root)"
    exit 2
fi

echo "=== Sprint 1 D7 audit: $(basename "$CPP") ==="
echo

# ────────────────────────────────────────────────────────────────────────
# Check 1: D4 SPIKE comment integrity
# ────────────────────────────────────────────────────────────────────────
# REGRESSION TRAP — Phase B Sprint 1 D11 downgraded `stateMutex_` from
# `std::recursive_mutex` to `std::mutex`. The original 3 commented lock_guard
# lines in HookEngine.cpp (LowLevelKeyboardProc / WinEventProc /
# LowLevelMouseProc) referenced the old `recursive_mutex` type which no
# longer exists, so any "cleanup" attempt that uncomments them triggers
# a compile error. That is the intended trap.
#
# Wave 3 PR 3.2 (2026-05-24) moved WinEventProc out of HookEngine into
# FocusOwner.cpp; FocusOwner has no stateMutex_, so the WinEventProc trap
# is moot there. Remaining traps in HookEngine.cpp: LowLevelKeyboardProc +
# LowLevelMouseProc (≥2).
#
# This check enforces the lines stay commented — verifying both that
# someone hasn't uncommented (which would fail compile anyway) and that
# someone hasn't deleted the trap entirely (which would lose the
# regression marker for the eventual mutex removal). If a future
# reviewer flags these as "dangling references", point them here and
# at the in-source comments above each line.

echo "Check 1: D4 SPIKE comment integrity (≥2 commented lock_guard lines, post Wave 3 PR 3.2)"
spike_commented=$(grep -cE "^\s*//\s*std::lock_guard<std::recursive_mutex>" "$CPP")
if [ "$spike_commented" -lt 2 ]; then
    echo "  FAIL: expected ≥2 commented lock_guard<recursive_mutex> lines, found $spike_commented"
    grep -nE "^\s*//\s*std::lock_guard<std::recursive_mutex>" "$CPP" || true
    errors=$((errors + 1))
else
    echo "  OK: $spike_commented commented lock_guard line(s) preserved"
fi

# ────────────────────────────────────────────────────────────────────────
# Check 2: No uncommented stateMutex_ in hook callback entry function bodies
# ────────────────────────────────────────────────────────────────────────
# Plan §B D7 scope: LL keyboard / LL mouse / WinEvent — the callbacks that
# run on hookThread_ (LL keyboard / mouse / RawInput WM_INPUT). WinEventProc
# is included for plan continuity even though current architecture installs
# it from main thread (WINEVENT_OUTOFCONTEXT runs on installer thread); the
# audit catches the case where a future refactor moves it back onto the
# hook thread without removing the lock.
#
# Sprint 1 D10 retired `FocusPollTimerProc` (the 200 ms `SetTimer` poll for
# CJK layout + foreground PID). Its body now lives in `OnTickPoll`, driven
# from `MainThreadWorker`'s tick branch. Worker-thread access to stateMutex_
# is fine — Rule #11 only forbids it on the LL hook thread.

HOOK_ENTRIES=(
    "LowLevelKeyboardProc"
    "WinEventProc"
    "LowLevelMouseProc"
    "RawInputWndProc"
)

echo
echo "Check 2: stateMutex_ not reachable from hook callback entries"
for fn in "${HOOK_ENTRIES[@]}"; do
    # Extract function body using awk: from the line containing "::fn(" through
    # the matching closing brace at column 0. This is approximate (assumes
    # consistent indentation — closing brace at column 0 — which the codebase
    # follows). Grep -v filters comment lines.
    body=$(awk -v fn="$fn" '
        $0 ~ ("HookEngine::" fn "[[:space:]]*\\(") { in_fn = 1 }
        in_fn { print }
        in_fn && /^\}/ { in_fn = 0 }
    ' "$CPP")
    if [ -z "$body" ]; then
        # Function not found — could be a static helper or moved. Skip with note.
        echo "  SKIP: $fn (not found in $CPP)"
        continue
    fi
    # Count uncommented stateMutex_ references in the body.
    refs=$(echo "$body" | grep -vE "^\s*//" | grep -c "stateMutex_" || true)
    if [ "$refs" -gt 0 ]; then
        echo "  FAIL: $fn body contains $refs uncommented stateMutex_ reference(s)"
        echo "$body" | grep -vE "^\s*//" | grep -nE "stateMutex_" || true
        errors=$((errors + 1))
    else
        echo "  OK: $fn — 0 uncommented stateMutex_ references"
    fi
done

# ────────────────────────────────────────────────────────────────────────
# Check 3: Migrated atomic fields use .load() / .store() exclusively
# ────────────────────────────────────────────────────────────────────────
# Fields migrated across D5 / D5.1 / D5.2 / D6. Any plain access (assignment
# or read) on these fields outside of an explicit `.load(` / `.store(` call
# violates Rule #11.3 and is flagged.
#
# False-positive sources we explicitly tolerate:
#   * Lines starting with `//` (full-line C++ comments)
#   * Lines starting with `///` (Doxygen-style)
#   * Lines containing only field name in a printf format string would also
#     match — we accept that and recommend tightening this filter only if
#     it triggers in practice.
#   * Lines bearing an inline `// audit-allow: <reason>` annotation —
#     used for legitimate pass-by-reference patterns like
#     `make_unique<Gate>(atomic_field_)` where the receiver stores a
#     const ref and uses `.load()` inside. The reason after the colon is
#     mandatory so reviewers see the justification without leaving the
#     file. See `docs/CODING_RULES/12-worker-thread-doctrine.md` §12.6.

# Sprint 2 D3 deleted: isConsoleApp_ — Console selection now flows through
# WindowClassification.isConsole → SplitDispatchInjector(5ms) by the factory.
# Sprint 2 D4 deleted: useEditMsgPath_ — replaced by HookEngine::IsSync-
# ReplaceChannel() proxy on injector_->SettleBudget()==0.
# Post-T3 ChannelTraits cleanup deleted: isElectronApp_ + needBaitChar_ —
# both flags moved onto IOutputInjector (HasMultiProcessRenderer() /
# NeedsBaitCharPrefix()) so the dispatch channel owns its own character.
ATOMIC_BOOLS="vietnameseMode_|isTsfApp_|isExcludedApp_|skipEmptyChar_|useClipboardPaste_|isOutlookApp_|macroEnabled_|macroInEnglish_|autoCaps_|autoCapsMacro_|tempOffMacroByEsc_|tempOffByAlt_"
ATOMIC_DWORD="excludedPid_"
ATOMIC_ENUM="currentMethod_"
ATOMIC_RCU="config_"
# Sprint 2 D2: injector_ is std::atomic<std::shared_ptr<IOutputInjector>>.
# Same discipline as config_ — access only via std::atomic_load/store, never
# via plain assignment or read.
ATOMIC_INJECTOR="injector_"

echo
echo "Check 3: atomic fields use .load()/.store() (no plain assignment or read)"

# Helper: count plain accesses for a field pattern.
# A "plain access" is any reference NOT followed by `.load(` or `.store(`,
# excluding C++ comments and the field's own declaration line.
audit_field_group() {
    local label="$1"
    local pattern="$2"
    local extra_exclude="$3"  # optional extra grep -vE pattern

    # Build the exclusion regex. Lines we don't count as violations:
    #   - .load( or .store( (the migrated atomic call sites)
    #   - C++ // comment lines (whole-line comments)
    #   - field declaration ("std::atomic<...>")
    #   - inline `// audit-allow:` annotation (see header comment above)
    #   - extra exclude (e.g. configEvent_ for the config_ check)
    local exclude="\\.(load|store)\\s*\\(|^\\s*[0-9]+:\\s*//|std::atomic|//\\s*audit-allow:"
    if [ -n "$extra_exclude" ]; then
        exclude="$exclude|$extra_exclude"
    fi

    local matches
    matches=$(grep -nE "\b($pattern)\b" "$CPP" | grep -vE "$exclude" || true)
    local count
    count=$(echo -n "$matches" | grep -c '^' || true)
    if [ "$count" -gt 0 ]; then
        echo "  FAIL [$label]: $count plain access(es)"
        echo "$matches" | head -10 | sed 's/^/    /'
        errors=$((errors + 1))
    else
        echo "  OK   [$label]"
    fi
}

audit_field_group "atomic<bool> primitives"  "$ATOMIC_BOOLS"  ""
audit_field_group "atomic<DWORD>"            "$ATOMIC_DWORD"  ""
audit_field_group "atomic<InputMethod>"      "$ATOMIC_ENUM"   ""
# config_ check excludes configEvent_ / configReloadCallback_ / config_t typedefs
audit_field_group "atomic<shared_ptr<TypingConfig>>" "$ATOMIC_RCU" "configEvent_|configReloadCallback_|TypingConfig"

# ────────────────────────────────────────────────────────────────────────
# Check 4: injector_ accessed only via std::atomic_load / std::atomic_store
# ────────────────────────────────────────────────────────────────────────
# Sprint 2 D2 introduced injector_ (RCU on std::shared_ptr<IOutputInjector>).
# Hot path readers MUST use std::atomic_load(&injector_) — plain
# `injector_->Replace(...)` would be a torn read on the shared_ptr control
# block and could invoke Replace on a destructed impl. Same regression-
# trap intent as Check 3 for config_.
echo
echo "Check 4: injector_ accessed only via std::atomic_load / std::atomic_store"
inj_violations=$(grep -nE "\binjector_\b" "$CPP" | \
    grep -vE "\\.(load|store)\\s*\\(|^\\s*[0-9]+:\\s*//|std::atomic|//\\s*audit-allow:" || true)
inj_count=$(echo -n "$inj_violations" | grep -c '^' || true)
if [ "$inj_count" -gt 0 ]; then
    echo "  FAIL: $inj_count plain access(es) to injector_ outside .load()/.store()"
    echo "$inj_violations" | head -10 | sed 's/^/    /'
    errors=$((errors + 1))
else
    echo "  OK"
fi
# Suppress unused-variable warning when ATOMIC_INJECTOR is reserved for future
# decomposition (e.g. dynamic_cast traits via the same regex helper).
: "${ATOMIC_INJECTOR:?}" >/dev/null

# ────────────────────────────────────────────────────────────────────────
# Check 5: QuickSyncFromSharedState hot path is lock-free
# ────────────────────────────────────────────────────────────────────────
# ProcessKeyDown calls QuickSyncFromSharedState on every keystroke from the
# LL hook thread. Rule #11.3 forbids the hook thread from waiting on a
# mutex contended with the main thread. The function MUST early-return
# before acquiring stateMutex_ on the common case (epoch unchanged).
#
# Heuristic: in the body of QuickSyncFromSharedState, an uncommented
# `return` statement must appear BEFORE the first uncommented
# `lock_guard<std::mutex> _lock(stateMutex_)` line. That return is the
# lock-free fast path; the lock guards only the slow path that handles
# an actual SharedState change.
#
# This check would have caught the pre-fix shape where the lock was
# acquired unconditionally at the top of the function (Pre-T3 review
# Minor 2, see docs/TODO.md).
echo
echo "Check 5: QuickSyncFromSharedState hot path returns before stateMutex_ lock"
qs_body=$(awk '
    /HookEngine::QuickSyncFromSharedState[[:space:]]*\(/ { in_fn = 1; next }
    in_fn { print }
    in_fn && /^\}/ { in_fn = 0 }
' "$CPP")
if [ -z "$qs_body" ]; then
    echo "  SKIP: QuickSyncFromSharedState body not found"
else
    # Strip whole-line C++ comments so commented patterns don't fool the heuristic.
    qs_clean=$(echo "$qs_body" | grep -vE "^\s*//")
    # Line numbers within the cleaned body for the lock and the first return.
    lock_line=$(echo "$qs_clean" | grep -nE "lock_guard<std::mutex>.*stateMutex_" | head -1 | cut -d: -f1)
    return_line=$(echo "$qs_clean" | grep -nE "\breturn[[:space:]]*;" | head -1 | cut -d: -f1)
    if [ -z "$lock_line" ]; then
        echo "  OK: QuickSyncFromSharedState no longer locks stateMutex_"
    elif [ -z "$return_line" ]; then
        echo "  FAIL: QuickSyncFromSharedState locks stateMutex_ with no early return"
        errors=$((errors + 1))
    elif [ "$return_line" -ge "$lock_line" ]; then
        echo "  FAIL: QuickSyncFromSharedState lock acquired before any early return"
        echo "        (lock at body line $lock_line, first return at body line $return_line)"
        echo "        Hook hot path must early-return on unchanged epoch BEFORE locking"
        errors=$((errors + 1))
    else
        echo "  OK: lock-free early return at body line $return_line precedes lock at $lock_line"
    fi
fi

# ────────────────────────────────────────────────────────────────────────
# Result
# ────────────────────────────────────────────────────────────────────────
echo
if [ "$errors" -gt 0 ]; then
    echo "FAIL: $errors check(s) failed — Phase B compliance regressed"
    exit 1
fi
echo "PASS: all D7 audit checks passed — Phase B compliance maintained"
exit 0
