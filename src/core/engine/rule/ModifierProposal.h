// src/core/engine/rule/ModifierProposal.h
//
// Abstract base for per-modifier sub-handler proposals (W8.1+).
//
// A ModifierProposal is the documented home for a single modifier action's
// dispatch — it carries NON-AUTHORITATIVE METADATA describing the modifier's
// relocation intent + a tryApply() entry point that delegates to the
// engine body (via IModifierSubExecutor).
//
// Design notes (W8.1 plan v3, post-round-3 review):
//   - Metadata methods (relocationKind) are DOCUMENTATION ONLY — they do NOT
//     enforce behaviour. The engine body is source of truth for the actual
//     speculation/apply/relocate sequence. Tests pin metadata-to-body parity;
//     if either drifts, tests catch it.
//   - The ad09f15 invariant "speculate path mirrors apply path" still lives
//     in HandleAdjacentCircumflex body's per-pre-state branching plus the
//     WouldBeValidSyllable comment block. This abstraction does NOT lift
//     the invariant into a compile-time contract.
//   - The cumulative win across W8.1-W8.5 is organisational:
//       `grep RelocationKind:: src/core/engine/rule/` shows the modifier
//       policy map at-a-glance, and parity tests across Telex/VNI proposals
//       surface drift via class-level diff.
#pragma once

#include <cstdint>

#include "core/engine/TypingAction.h"

namespace NextKey::EngineRule {

// Documents how each modifier interacts with the existing tone after applying
// its base change. NOT enforced at the class level — the engine body's
// actual RelocateToneTo* calls are the source of truth. Tests pin the
// mapping so drift between metadata and body surfaces in CI.
//
// Classification criterion (crisp — apply when adding W9+ proposals):
//   1. Body has NO RelocateToneTo* calls anywhere       → None
//   2. Body calls RelocateToneToTarget ONLY, always (no per-pre-state gate
//      and no other RelocateToneTo* function)           → TargetTone
//   3. Body calls RelocateToneToHornVowel ONLY, always  → HornVowel
//   4. Body has gated/branched relocate calls (`if (needsRelocate) ...`,
//      multiple paths with different gating, or branches that mix relocate
//      functions)                                       → Conditional
//
// Why crisp criterion matters: the metadata's audit-surface value depends
// on consistent classification. W8.5's VniHorn was initially mis-declared
// HornVowel (it actually uses TargetTone) — the fix corrected the function,
// but post-W8 review (2026-05-25) surfaced a deeper issue: bodies with
// `if (needsRelocate) RelocateToneToTarget()` were called TargetTone in
// some proposals and Conditional in others. Option B (this rewrite) makes
// "has gating" the primary discriminator so the next contributor can't
// repeat the W8.5 mistake.
enum class RelocationKind : uint8_t {
    // Body never calls any RelocateToneTo* function. Tone stays where it
    // is, or the modifier doesn't touch tone-bearing state.
    // Examples (single-criterion: zero relocate calls in body):
    //   - HandleStrokeD (TypingEngine.cpp:1327): mod toggle on `d`,
    //     no vowel cluster change → no relocation needed.
    //   - HandleHornInsert (TypingEngine.cpp:859): appends a new
    //     Modifier::Horn state; nothing to relocate from.
    None,

    // Body calls RelocateToneToTarget UNCONDITIONALLY on every path that
    // mutates state. No per-pre-state gating, no mix with other relocate
    // functions. Currently UNUSED — every Telex/VNI proposal that uses
    // RelocateToneToTarget has at least one gated branch and falls under
    // Conditional. Reserved for proposals where every branch
    // unconditionally relocates (rare; e.g., a future "always promote"
    // modifier).
    TargetTone,

    // Body calls RelocateToneToHornVowel UNCONDITIONALLY on every path
    // that mutates state. Example:
    //   - HandleHornW (TypingEngine.cpp:1070): 6 unconditional
    //     RelocateToneToHornVowel calls across P1/P2/P5/P6 branches.
    //     None of them are gated by `if (needsRelocate)`.
    HornVowel,

    // Body has gated relocate calls (`if (needsRelocate) ...`) OR mixes
    // relocate functions across branches OR has branches that diverge in
    // whether they relocate at all. Auditor MUST read the body to
    // understand the policy — single tag is insufficient.
    //
    // Examples post-W8.5 (all use RelocateToneToTarget when relocating;
    // each has at least one gated branch):
    //   - HandleAdjacentCircumflex adjacent branch: gated by needsRelocate
    //     (pre-state == ValidPrefix); free-marking branch: unconditional.
    //   - ProcessVniVowelModifier Pass 1: gated by needsRelocate; Pass 1.5
    //     (modifier switching): unconditional.
    //   - HandleVniHorn: uo/uu paths unconditional; generic fallback
    //     (delegates to ProcessVniVowelModifier) gated.
    //
    // Why this is the audit-focal bucket: gated relocate is the source of
    // speculate-apply parity bugs (ad09f15, c6369dd, W8.5 fix). Proposals
    // declaring Conditional are the ones reviewers should pay extra
    // attention to when changing the body.
    Conditional,
};

class ModifierProposal {
public:
    virtual ~ModifierProposal() = default;

    // Declarative metadata — DOCUMENTATION + test-pinning only. Not enforced.
    [[nodiscard]] virtual RelocationKind relocationKind() const noexcept = 0;

    // The only behaviour-bearing method. Delegates to the engine body via
    // IModifierSubExecutor; the body remains the source of truth for the
    // actual speculation/apply/relocate sequence.
    [[nodiscard]] virtual bool tryApply(TypingAction action, wchar_t keyChar) = 0;
};

}  // namespace NextKey::EngineRule
