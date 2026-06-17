// VKey - Tests for the spreadsheet-formula segment FSM
// SPDX-License-Identifier: AGPL-3.0-only
//
// Proves the formula-detection state machine is fully deterministic — the
// transitions HookEngine::UpdateFormulaSegment relies on. (HookEngine itself
// is Win-only; this is the Linux-runnable contract.)

#include <gtest/gtest.h>

#include "core/FormulaSegmentDecision.h"

namespace NextKey {
namespace {

using K = FormulaKeyKind;

// Fold a sequence of key-kinds through the FSM from the default initial state.
FormulaSegmentState Fold(std::initializer_list<FormulaKeyKind> kinds) {
    FormulaSegmentState s{};
    for (FormulaKeyKind k : kinds) s = NextFormulaSegmentState(s, k);
    return s;
}

// ── Initial state ──────────────────────────────────────────────────────────
TEST(FormulaSegmentDecision, InitialStateIsArmedNonFormula) {
    FormulaSegmentState s{};
    EXPECT_TRUE(s.atSegmentStart);
    EXPECT_FALSE(s.inFormula);
}

// ── Single transitions from segment start ───────────────────────────────────
TEST(FormulaSegmentDecision, EqualsAtStartOpensFormula) {
    FormulaSegmentState s = Fold({K::EqualsStart});
    EXPECT_FALSE(s.atSegmentStart);
    EXPECT_TRUE(s.inFormula);
}

TEST(FormulaSegmentDecision, NonEqualsAtStartIsPlainText) {
    FormulaSegmentState s = Fold({K::OtherContent});
    EXPECT_FALSE(s.atSegmentStart);
    EXPECT_FALSE(s.inFormula);
}

// ── The reported bug: "=if" stays a formula through the tone key ────────────
TEST(FormulaSegmentDecision, EqualsThenLettersStaysFormula) {
    // '=' , 'i' , 'f'  → formula held across the whole word.
    FormulaSegmentState s = Fold({K::EqualsStart, K::OtherContent, K::OtherContent});
    EXPECT_TRUE(s.inFormula);
}

// ── Contrast: "ef" (no leading '=') never becomes a formula ─────────────────
TEST(FormulaSegmentDecision, LetterFirstNeverFormulaEvenWithLaterEquals) {
    // 'e' opens a plain-text segment; a later '=' ("e=...") must NOT flip it.
    FormulaSegmentState s = Fold({K::OtherContent, K::EqualsStart});
    EXPECT_FALSE(s.inFormula);
}

// ── '=' mid-formula (comparison, e.g. "=a=b") keeps formula-ness ────────────
TEST(FormulaSegmentDecision, EqualsMidFormulaKeepsFormula) {
    FormulaSegmentState s = Fold({K::EqualsStart, K::OtherContent, K::EqualsStart});
    EXPECT_TRUE(s.inFormula);
    EXPECT_FALSE(s.atSegmentStart);
}

// ── Passive keys never change state ─────────────────────────────────────────
TEST(FormulaSegmentDecision, PassiveAtStartLeavesArmed) {
    // Shift/Ctrl/Backspace before any content must not consume the segment start.
    FormulaSegmentState s = Fold({K::Passive, K::Passive});
    EXPECT_TRUE(s.atSegmentStart);
    EXPECT_FALSE(s.inFormula);
    // ...so a following '=' still opens the formula.
    s = NextFormulaSegmentState(s, K::EqualsStart);
    EXPECT_TRUE(s.inFormula);
}

TEST(FormulaSegmentDecision, PassiveMidFormulaPreservesFormula) {
    FormulaSegmentState s = Fold({K::EqualsStart, K::Passive});
    EXPECT_TRUE(s.inFormula);
}

// ── Boundary resets to armed/non-formula ────────────────────────────────────
TEST(FormulaSegmentDecision, BoundaryResetsState) {
    FormulaSegmentState s = Fold({K::EqualsStart, K::OtherContent, K::Boundary});
    EXPECT_TRUE(s.atSegmentStart);
    EXPECT_FALSE(s.inFormula);
}

// ── Navigate (arrows / Home / End) — Excel point-mode keeps the formula ─────
TEST(FormulaSegmentDecision, NavigateInsideFormulaPreservesFormula) {
    // "=SUM(" then Right/Left to point at a cell — the formula is NOT closed,
    // so inFormula must stay true (else the bait re-emits inside the "=..." cell).
    FormulaSegmentState s = Fold({K::EqualsStart, K::OtherContent, K::Navigate});
    EXPECT_TRUE(s.inFormula);
    EXPECT_FALSE(s.atSegmentStart);
    // Subsequent content (more of the formula) stays a formula.
    s = NextFormulaSegmentState(s, K::OtherContent);
    EXPECT_TRUE(s.inFormula);
}

TEST(FormulaSegmentDecision, NavigateOutsideFormulaActsLikeBoundary) {
    // Plain-text cell then an arrow = grid navigation to a fresh cell → re-arm.
    FormulaSegmentState s = Fold({K::OtherContent, K::Navigate});
    EXPECT_TRUE(s.atSegmentStart);
    EXPECT_FALSE(s.inFormula);
    // ...and the new cell can open a formula.
    s = NextFormulaSegmentState(s, K::EqualsStart);
    EXPECT_TRUE(s.inFormula);
}

TEST(FormulaSegmentDecision, NavigateFromArmedStaysArmed) {
    // Arrow with nothing typed yet (grid navigation) leaves the segment armed.
    FormulaSegmentState s = Fold({K::Navigate});
    EXPECT_TRUE(s.atSegmentStart);
    EXPECT_FALSE(s.inFormula);
}

// ── Full multi-cell session: Enter between cells re-arms detection ──────────
TEST(FormulaSegmentDecision, MultiCellSessionReArmsAfterEnter) {
    // Cell 1: "=if"  → formula
    FormulaSegmentState s = Fold({K::EqualsStart, K::OtherContent, K::OtherContent});
    EXPECT_TRUE(s.inFormula);
    // Enter → next cell.
    s = NextFormulaSegmentState(s, K::Boundary);
    // Cell 2: "hi" (plain text) → not a formula, bait stays enabled.
    s = NextFormulaSegmentState(s, K::OtherContent);
    s = NextFormulaSegmentState(s, K::OtherContent);
    EXPECT_FALSE(s.inFormula);
    // Enter → cell 3.
    s = NextFormulaSegmentState(s, K::Boundary);
    // Cell 3: "=sum" → formula again.
    s = NextFormulaSegmentState(s, K::EqualsStart);
    s = NextFormulaSegmentState(s, K::OtherContent);
    EXPECT_TRUE(s.inFormula);
}

// ── Determinism: same input sequence always yields the same state ───────────
TEST(FormulaSegmentDecision, Deterministic) {
    const auto seq = {K::Boundary, K::EqualsStart, K::OtherContent,
                      K::Passive, K::OtherContent};
    EXPECT_EQ(Fold(seq).inFormula, Fold(seq).inFormula);
    EXPECT_EQ(Fold(seq).atSegmentStart, Fold(seq).atSegmentStart);
    EXPECT_TRUE(Fold(seq).inFormula);
}

}  // namespace
}  // namespace NextKey
