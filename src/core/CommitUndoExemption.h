// VKey - Commit-undo cancellation exemption rule
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Pure predicate: given a keystroke, returns true if the key should be
// EXEMPT from the cancel-Primed branches in
// `HookEngine::HandleCommitUndo`. Two cancel sites share this rule:
//
//   1. Synth-guard (line ~1061): full `CancelCommitUndo()` when injector
//      synth events are still in flight (`synthEventsPending_ > 0 &&
//      elapsed < settleMs`). Non-exempt keys wipe `commitStack_`.
//
//   2. Catch-all else (line ~1124): plain demotion to Idle state
//      (commitStack_ preserved) for any key not matching alpha / VNI
//      digit / VK_BACK.
//
// Both sites must exempt the same key classes so the post-BS recovery
// paths (modifier-letter replay, ESC restore-raw) can fire.
//
// Extracted from HookEngine.cpp for Linux GTest coverage: HookEngine.cpp
// is Win32-only and not linked into the cross-platform VKeyTests target.
// See `docs/plans/2026-05-17-esc-restore-raw-post-bs-design.md` §9 and
// memory `project_commit_undo_synth_guard_exemption.md`.

#pragma once

#include "core/config/TypingConfig.h"  // InputMethod
#include "core/engine/TypingAction.h"  // TypingAction (for IsCommitUndoExemptAction)
#include <cstdint>

namespace NextKey {

/// Returns true when `action` modifies the previously-committed word
/// rather than starting a new one. Used by UserDefined customKeyMap
/// lookups to decide whether to exempt a custom-bound key from the
/// commit-undo cancel branches.
///
/// Modifier semantic class (matches Telex/SimpleTelex/Combined letter
/// list `s/f/r/x/j/z/a/e/o/w/d` and VNI digit list `0-9`):
///   - Tone: ClearTone, ToneAcute..ToneDot.
///   - Telex modifiers: CircumflexA/E/O, HornW, HornInsertO/U, StrokeD.
///   - VNI modifiers: VniCircumflex, VniHorn, VniBreve, VniStroke.
///   - UserDefined-only: HornOrInsertU, HornOrInsertUNoStart, UndoAllMarks.
///
/// `Insert*` direct-char actions (InsertABreve etc.) are excluded — they
/// synthesise a fresh char rather than modify the prior word, so the
/// caller should treat them as new-word intent.
[[nodiscard]] constexpr bool IsCommitUndoExemptAction(TypingAction action) noexcept {
    if (IsToneAction(action)) return true;
    if (IsTelexModifierAction(action)) return true;
    if (IsVniModifierAction(action)) return true;
    if (action == TypingAction::HornOrInsertU ||
        action == TypingAction::HornOrInsertUNoStart ||
        action == TypingAction::UndoAllMarks) return true;
    return false;
}

/// Returns true when `vkCode` should bypass the cancel-Primed branches.
///
/// Exempt classes (all share the semantic "key modifies the previously
/// committed word"; the engine handles the actual transform after
/// `ReplayCommittedChars` restores state):
///   - Telex tone + modifier letters `s/f/r/x/j/z/a/e/o/w/d` (active in
///     Telex / SimpleTelex / Combined). The engine's `IsTelexMode()`
///     returns true for all three; SimpleTelex only differs in bracket
///     and `w` standalone handling, both still "modify previous word".
///   - VNI digits `0-9` without Shift (active in VNI / Combined). All
///     ten digits map to a tone or modifier action (0=clear, 1-5=tones,
///     6=circumflex, 7=horn, 8=breve, 9=stroke). Shift filters out
///     punctuation variants (Shift+1=`!` etc.).
///   - UserDefined: caller resolves `vkCode` → ASCII → customKeyMap
///     action and passes `isCustomModifier=IsCommitUndoExemptAction(...)`.
///   - VK_ESCAPE when `escIsCancelTrigger` is true — same semantic class
///     (replaces composed Vietnamese with raw input).
///
/// All other keys (literal letters outside the modifier list, navigation,
/// F-keys, etc.) return false — caller must demote / cancel commit-undo
/// state.
[[nodiscard]] constexpr bool IsCommitUndoExemptKey(
    uint32_t vkCode,
    InputMethod method,
    bool shiftHeld,
    bool escIsCancelTrigger,
    bool isCustomModifier = false) noexcept {
    constexpr uint32_t kVkEscape = 0x1B;

    // Telex modifier letter set: tones (s/f/r/x/j/z) + circumflex/horn/
    // stroke triggers (a/e/o/w/d). Each "modifies previous word" when
    // the engine sees the right adjacent char; otherwise replay is a
    // no-op (engine appends as literal letter, same as new-word path).
    const bool isTelexModifierLetter =
        (method == InputMethod::Telex ||
         method == InputMethod::SimpleTelex ||
         method == InputMethod::Combined) &&
        (vkCode == 'S' || vkCode == 'F' || vkCode == 'R' ||
         vkCode == 'X' || vkCode == 'J' || vkCode == 'Z' ||
         vkCode == 'A' || vkCode == 'E' || vkCode == 'O' ||
         vkCode == 'W' || vkCode == 'D');

    // VNI digit set: 0 (clear-tone), 1-5 (tones), 6-9 (modifiers).
    const bool isVniModifierDigit =
        (method == InputMethod::VNI || method == InputMethod::Combined) &&
        vkCode >= '0' && vkCode <= '9' &&
        !shiftHeld;

    const bool isUserDefinedModifier =
        (method == InputMethod::UserDefined) && isCustomModifier;

    const bool isEscRestoreRawKey =
        (vkCode == kVkEscape) && escIsCancelTrigger;

    return isTelexModifierLetter || isVniModifierDigit ||
           isUserDefinedModifier || isEscRestoreRawKey;
}

}  // namespace NextKey
