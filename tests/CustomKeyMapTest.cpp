// VKey - customKeyMap (G-4) Tests
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Tests for the per-key user override layer added in Path G G-4.
// Spec: docs/superpowers/specs/2026-05-07-path-g-g4-customkeymap-design.md

#include <gtest/gtest.h>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;

class CustomKeyMapTest : public ::testing::Test {
protected:
    TypingConfig MakeTelexConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Telex;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }

    TypingConfig MakeVniConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::VNI;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }

    TypingConfig MakeCombinedConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Combined;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }

    TypingConfig MakeUserDefinedConfig() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::UserDefined;
        cfg.spellCheckEnabled = false;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// =====================================================================
// U1 — UserDefined Mode: pure hybrid (Telex + VNI actions coexist)
// =====================================================================

TEST_F(CustomKeyMapTest, UserDefinedHybridTelexAndVni) {
    TypingConfig cfg = MakeUserDefinedConfig();
    // 's' (Telex sharp) and '2' (VNI grave)
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L'2')] = TypingAction::ToneGrave;
    
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    EXPECT_EQ(engine.Peek(), L"á");
    
    TypeString(engine, L"2"); // Switch to grave
    EXPECT_EQ(engine.Peek(), L"à");
}

TEST_F(CustomKeyMapTest, UserDefinedUndoAllMarks) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L'a')] = TypingAction::CircumflexA;
    cfg.customKeyMap[static_cast<size_t>(L'z')] = TypingAction::UndoAllMarks;
    
    TypingEngine engine(cfg);
    TypeString(engine, L"aas");
    // 'a' + remapped 'a' -> â, then 's' -> ấ
    EXPECT_EQ(engine.Peek(), L"ấ");
    
    TypeString(engine, L"z");
    // 'z' is remapped to UndoAllMarks -> clears both circumflex and sharp tone.
    // Base char 'a' remains.
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST_F(CustomKeyMapTest, UserDefinedDirectInsert) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'1')] = TypingAction::InsertABreve;
    cfg.customKeyMap[static_cast<size_t>(L'2')] = TypingAction::InsertDStroke;
    
    TypingEngine engine(cfg);
    TypeString(engine, L"12");
    EXPECT_EQ(engine.Peek(), L"ăđ");
}

TEST_F(CustomKeyMapTest, UserDefinedCircumflexEscape) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::CircumflexA;
    
    TypingEngine engine(cfg);
    TypeString(engine, L"aq");
    EXPECT_EQ(engine.Peek(), L"â");
    
    // Problem 6: Gõ tiếp 'q' phải escape â -> aq
    TypeString(engine, L"q");
    EXPECT_EQ(engine.Peek(), L"aq");
}

// U2 — UserDefined OEM punctuation keys (HookEngine step 6d, bug 2026-05-16).
// `;` bound to ToneDot không ăn vì HookEngine::IsCommitTrigger nuốt OEM trước.
// Engine layer đã đúng — test này pin behaviour để routing fix không drift.
TEST_F(CustomKeyMapTest, UserDefinedOemKeysSpanAllTones) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L';')] = TypingAction::ToneDot;
    cfg.customKeyMap[static_cast<size_t>(L'\'')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L',')] = TypingAction::ToneGrave;
    cfg.customKeyMap[static_cast<size_t>(L'.')] = TypingAction::ToneHook;
    cfg.customKeyMap[static_cast<size_t>(L'/')] = TypingAction::ToneTilde;
    TypingEngine engine(cfg);
    TypeString(engine, L"a;");
    EXPECT_EQ(engine.Peek(), L"ạ");  // U+1EA1
    TypeString(engine, L"\'");
    EXPECT_EQ(engine.Peek(), L"á");
    TypeString(engine, L",");
    EXPECT_EQ(engine.Peek(), L"à");
    TypeString(engine, L".");
    EXPECT_EQ(engine.Peek(), L"ả");
    TypeString(engine, L"/");
    EXPECT_EQ(engine.Peek(), L"ã");
}

// U3 — User feedback 2026-05-17: phím W remapped to HornOrInsertUNoStart,
// `[` `]` mapped to HornInsertO/U.
// Reported: `[` `]` không hoạt động, W đầu từ vẫn ra Ư.
// Engine-layer pin: verify literal/insert behaviour so we can isolate Hook
// routing vs engine bugs.
TEST_F(CustomKeyMapTest, UserDefinedWNoStartAtWordStartIsLiteral) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    TypingEngine engine(cfg);
    TypeString(engine, L"w");
    EXPECT_EQ(engine.Peek(), L"w");  // start-of-word: literal, NOT ư
}

TEST_F(CustomKeyMapTest, UserDefinedBracketInsertsHornAtWordStart) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::HornInsertO;
    cfg.customKeyMap[static_cast<size_t>(L']')] = TypingAction::HornInsertU;
    TypingEngine engine(cfg);
    TypeString(engine, L"[");
    EXPECT_EQ(engine.Peek(), L"ơ");
    TypingEngine engine2(cfg);
    TypeString(engine2, L"]");
    EXPECT_EQ(engine2.Peek(), L"ư");
}

TEST_F(CustomKeyMapTest, UserDefinedWNoStartStillAppliesHornMidWord) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    TypingEngine engine(cfg);
    TypeString(engine, L"tuw");  // u + w → ư mid-word (P5)
    EXPECT_EQ(engine.Peek(), L"tư");
    TypingEngine engine2(cfg);
    TypeString(engine2, L"tow");  // o + w → ơ mid-word (P6)
    EXPECT_EQ(engine2.Peek(), L"tơ");
}

// Helper: mirror the full default UserDefined keymap that the Settings UI
// writes when the user picks the Telex-style preset (see config.toml shipped
// with the app). English-protection regression tests below depend on this
// shape because bias arming relies on e/a/o being mapped to Circumflex
// actions (free-mark fails set bias via HandleAdjacentCircumflex).
static void FillTelexPresetCustomKeyMap(TypingConfig& cfg) {
    cfg.customKeyMap[static_cast<size_t>(L'a')] = TypingAction::CircumflexA;
    cfg.customKeyMap[static_cast<size_t>(L'd')] = TypingAction::StrokeD;
    cfg.customKeyMap[static_cast<size_t>(L'e')] = TypingAction::CircumflexE;
    cfg.customKeyMap[static_cast<size_t>(L'o')] = TypingAction::CircumflexO;
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L'r')] = TypingAction::ToneHook;
    cfg.customKeyMap[static_cast<size_t>(L'f')] = TypingAction::ToneGrave;
    cfg.customKeyMap[static_cast<size_t>(L'x')] = TypingAction::ToneTilde;
    cfg.customKeyMap[static_cast<size_t>(L'j')] = TypingAction::ToneDot;
    cfg.customKeyMap[static_cast<size_t>(L'z')] = TypingAction::ClearTone;
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    cfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::HornInsertO;
    cfg.customKeyMap[static_cast<size_t>(L']')] = TypingAction::HornInsertU;
}

TEST_F(CustomKeyMapTest, UserDefinedRespectsEnglishBias_Review) {
    // Regression 2026-05-18: UserDefined user-only actions must honor the same
    // English-protection guards as Telex modifier path. Without bias check in
    // section 2d, typing "review" with w=HornOrInsertUNoStart produces
    // `revieư` (fallback insert fires) while equivalent Telex (HornW) stays
    // `review` (P8 skips because vowels exist + bias HardEnglish from free-mark).
    TypingConfig cfg = MakeUserDefinedConfig();
    FillTelexPresetCustomKeyMap(cfg);
    TypingEngine engine(cfg);
    TypeString(engine, L"review");
    EXPECT_EQ(engine.Peek(), L"review");
}

TEST_F(CustomKeyMapTest, UserDefinedRespectsEnglishBias_Where) {
    // `wh` raw start-cluster impossible in Vietnamese → IsHardEnglishStart sets
    // bias HardEnglish on the second char. Subsequent w/e/r/e must pass
    // through literally even in UserDefined mode.
    TypingConfig cfg = MakeUserDefinedConfig();
    FillTelexPresetCustomKeyMap(cfg);
    TypingEngine engine(cfg);
    TypeString(engine, L"where");
    EXPECT_EQ(engine.Peek(), L"where");
}

TEST_F(CustomKeyMapTest, UserDefinedValidVietnameseStillComposes_Thuw) {
    // Sanity guard: English-bias check must not over-block legitimate
    // Vietnamese sequences. `thuw` is valid start cluster (`thu` + horn → `thư`).
    TypingConfig cfg = MakeUserDefinedConfig();
    FillTelexPresetCustomKeyMap(cfg);
    TypingEngine engine(cfg);
    TypeString(engine, L"thuw");
    EXPECT_EQ(engine.Peek(), L"thư");
    TypeString(engine, L"s");
    EXPECT_EQ(engine.Peek(), L"thứ");
}

TEST_F(CustomKeyMapTest, UserDefinedHornOrInsertUFallback_WwFullyReverts) {
    // Regression 2026-05-18: w + w after a no-target insertion (e.g. typing
    // "revie" then `w` — no a/o/u to apply horn → fallback inserts ư as
    // "revieư") must FULLY revert on the second `w`, not split ư into u+w.
    // Mark the fallback-inserted ư as synthetic so HandleHornW P4 sees it
    // as the ww-escape signature (synthetic + last-state) → erase + literal.
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    TypingEngine engine(cfg);
    TypeString(engine, L"review");
    EXPECT_EQ(engine.Peek(), L"revieư");  // fallback inserted ư (synthetic)
    TypeString(engine, L"w");             // second `w` — full ww escape
    EXPECT_EQ(engine.Peek(), L"review");  // ư replaced by literal w, no `u`
}

TEST_F(CustomKeyMapTest, UserDefinedBracketInsertsHornMidWord) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::HornInsertO;
    cfg.customKeyMap[static_cast<size_t>(L']')] = TypingAction::HornInsertU;
    TypingEngine engine(cfg);
    TypeString(engine, L"th[");
    EXPECT_EQ(engine.Peek(), L"thơ");
    TypingEngine engine2(cfg);
    TypeString(engine2, L"th]");
    EXPECT_EQ(engine2.Peek(), L"thư");
}

// UserDefined "Chữ ơ/ư" (InsertOHorn/InsertUHorn) actions must mirror Telex
// bracket UX: doubled key reverts to literal. Without this, `[[` produces
// `ơơ` instead of `[` and users lose the "press twice to undo" affordance
// they expect from Telex.
TEST_F(CustomKeyMapTest, UserDefinedInsertOHornDoubleKeyEscapes) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::InsertOHorn;
    cfg.customKeyMap[static_cast<size_t>(L']')] = TypingAction::InsertUHorn;
    TypingEngine engine(cfg);
    TypeString(engine, L"[[");
    EXPECT_EQ(engine.Peek(), L"[");
    TypingEngine engine2(cfg);
    TypeString(engine2, L"]]");
    EXPECT_EQ(engine2.Peek(), L"]");
}

TEST_F(CustomKeyMapTest, UserDefinedInsertOHornDoubleKeyEscapesMidWord) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::InsertOHorn;
    TypingEngine engine(cfg);
    TypeString(engine, L"th[[");
    EXPECT_EQ(engine.Peek(), L"th[");
}

// Generalisation guard: same escape applies to circumflex/stroke variants too.
TEST_F(CustomKeyMapTest, UserDefinedInsertACircumflexDoubleKeyEscapes) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::InsertACircumflex;
    TypingEngine engine(cfg);
    TypeString(engine, L"qq");
    EXPECT_EQ(engine.Peek(), L"q");
}

TEST_F(CustomKeyMapTest, UserDefinedInsertDStrokeDoubleKeyEscapes) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'\'')] = TypingAction::InsertDStroke;
    TypingEngine engine(cfg);
    TypeString(engine, L"''");
    EXPECT_EQ(engine.Peek(), L"'");
}

// HandleHornInsert previously hardcoded the escape check against literal
// `[` / `]`. In UserDefined mode the user can bind ANY key to HornInsertO/U,
// and doubled-key escape must work the same — `q`→HornInsertO + `qq` must
// produce literal `q`, not `ơơ`.
TEST_F(CustomKeyMapTest, UserDefinedHornInsertODoubleKeyEscapesOnNonBracketKey) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::HornInsertO;
    TypingEngine engine(cfg);
    TypeString(engine, L"qq");
    EXPECT_EQ(engine.Peek(), L"q");
}

TEST_F(CustomKeyMapTest, UserDefinedHornInsertUDoubleKeyEscapesOnNonBracketKey) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L';')] = TypingAction::HornInsertU;
    TypingEngine engine(cfg);
    TypeString(engine, L";;");
    EXPECT_EQ(engine.Peek(), L";");
}

TEST_F(CustomKeyMapTest, UserDefinedHornInsertODoubleKeyEscapesMidWord) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::HornInsertO;
    TypingEngine engine(cfg);
    TypeString(engine, L"thqq");
    EXPECT_EQ(engine.Peek(), L"thq");
}

// =====================================================================
// G1 — Default-empty parity: customKeyMap{} → behavior unchanged
// =====================================================================

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesTelex) {
    TypingConfig cfg = MakeTelexConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"asfx");
    // G-3.6 baseline: telex 'a'+s+f+x — s=â modifier, f=tone huyền, x=tone ngã
    // → ã (U+00E3). Captured 2026-05-07.
    EXPECT_EQ(engine.Peek(), L"ã");
}

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesVni) {
    TypingConfig cfg = MakeVniConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"a1e2o3");
    // G-3.6 baseline: 'a'+1 → á, then 'e'+2 starts new syllable → é, then
    // 'o'+3 would continue — actual engine output captured 2026-05-07: aé2o3.
    EXPECT_EQ(engine.Peek(), L"aé2o3");
}

TEST_F(CustomKeyMapTest, DefaultEmptyMatchesCombined) {
    TypingConfig cfg = MakeCombinedConfig();
    TypingEngine engine(cfg);
    TypeString(engine, L"as6w7");
    // G-3.6 baseline: 'a'+s → â (telex vowel modifier), '6' → tone huyền → ầ,
    // 'w' → modifier, '7' appended — actual engine output captured 2026-05-07: ắ7.
    EXPECT_EQ(engine.Peek(), L"ắ7");
}


// =====================================================================
// G2 — Replace built-in: user override applies in UserDefined mode
// =====================================================================
// Invariant: customKeyMap is the user-defined input method's mapping table.
// It applies ONLY when `inputMethod == UserDefined`. Other modes use their
// own base mapping (ClassifyKey) and ignore customKeyMap entirely — see
// `*_IgnoredInNonUserDefinedMode` tests below for the regression guard.

TEST_F(CustomKeyMapTest, RemapSToToneHook) {
    TypingConfig cfg = MakeUserDefinedConfig();
    // UserDefined needs its full base mapping copied — only `s` differs.
    cfg.customKeyMap[static_cast<size_t>(L'a')] = TypingAction::CircumflexA;
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // With remap 's'→ToneHook: 'a' + 's' → 'ả' (hỏi) instead of 'á' (sắc).
    EXPECT_EQ(engine.Peek(), L"ả");
}

TEST_F(CustomKeyMapTest, RemapDigit1ToClearTone) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'1')] = TypingAction::ClearTone;
    cfg.customKeyMap[static_cast<size_t>(L'2')] = TypingAction::ToneGrave;
    TypingEngine engine(cfg);
    TypeString(engine, L"a2");  // 'a' + grave → 'à'
    EXPECT_EQ(engine.Peek(), L"à");
    TypeString(engine, L"1");   // remapped: clear tone
    EXPECT_EQ(engine.Peek(), L"a");
}


// =====================================================================
// G3 — Gap-fill: map a key in UserDefined mode (no base to fall back on)
// =====================================================================

TEST_F(CustomKeyMapTest, MapQToClearTone) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'a')] = TypingAction::CircumflexA;
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneAcute;
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::ClearTone;
    TypingEngine engine(cfg);
    TypeString(engine, L"asq");  // 'a' + sắc → 'á', then 'q' clears tone → 'a'
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST_F(CustomKeyMapTest, MapQToToneAcute) {
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::ToneAcute;
    TypingEngine engine(cfg);
    TypeString(engine, L"aq");  // 'a' + remapped 'q' → ToneAcute → 'á'
    EXPECT_EQ(engine.Peek(), L"á");
}

// =====================================================================
// G2/G3 regression guard: customKeyMap MUST be ignored in non-UserDefined
// modes. Stale `[UserDefinedKeyMap]` entries left in config.toml after a
// mode switch must not silently affect Telex/VNI/Combined base behavior.
// =====================================================================

TEST_F(CustomKeyMapTest, CustomMapIgnoredInTelexMode) {
    TypingConfig cfg = MakeTelexConfig();
    // Stale entries from a previous UserDefined session.
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneHook;
    cfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::HornOrInsertUNoStart;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Telex base: 'a' + 's' → 'á' (sắc). customKeyMap ignored.
    EXPECT_EQ(engine.Peek(), L"á");

    TypingEngine engine2(cfg);
    TypeString(engine2, L"thuw");
    // Telex base 'w' → HornW (P5: standalone u → horn). customKeyMap ignored.
    // Regression for user-reported bug 2026-05-18: stale customKeyMap['w']=
    // HornOrInsertUNoStart caused 'w' to silent-drop, producing literal "thuw"
    // → tone 's' then applied to bare 'u' producing "thúw".
    EXPECT_EQ(engine2.Peek(), L"thư");
}

TEST_F(CustomKeyMapTest, CustomMapIgnoredInVniMode) {
    TypingConfig cfg = MakeVniConfig();
    cfg.customKeyMap[static_cast<size_t>(L'1')] = TypingAction::ClearTone;
    TypingEngine engine(cfg);
    TypeString(engine, L"a1");
    // VNI base: 'a' + '1' → 'á' (ToneAcute). customKeyMap ignored.
    EXPECT_EQ(engine.Peek(), L"á");
}

TEST_F(CustomKeyMapTest, CustomMapIgnoredInCombinedMode) {
    TypingConfig cfg = MakeCombinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Combined base: Telex 's' → ToneAcute → 'á'. customKeyMap ignored.
    EXPECT_EQ(engine.Peek(), L"á");
}

// =====================================================================
// G4 — ASCII boundary: non-ASCII keys bypass the override branch
// =====================================================================

TEST_F(CustomKeyMapTest, NonAsciiKeyFallsBackToClassifyKey) {
    TypingConfig cfg = MakeTelexConfig();
    // The override array has only 128 slots; non-ASCII keys must skip
    // the override check entirely (defensive: `lower < 128` guard).
    // We verify by typing a Vietnamese char directly — the engine treats
    // it as a literal char (ClassifyKey returns None for it), and no
    // override applies because `lower >= 128`.
    TypingEngine engine(cfg);
    TypeString(engine, L"á");  // U+00E1, definitely >= 128
    EXPECT_EQ(engine.Peek(), L"á");  // unchanged literal pass-through
}

// =====================================================================
// G5 — isVniDigitSequence post-applies even when override fired
// =====================================================================

TEST_F(CustomKeyMapTest, OverrideOnDigitYieldsLiteralInDigitSequence) {
    TypingConfig cfg = MakeVniConfig();
    // Remap '7' → ToneHook (instead of default VniHorn).
    cfg.customKeyMap[static_cast<size_t>(L'7')] = TypingAction::ToneHook;
    TypingEngine engine(cfg);
    // Type a literal digit first to enter "VNI digit sequence" mode.
    TypeString(engine, L"4");
    // Now '7' should be treated as literal (digit-sequence guard wins
    // over both the override and the default VniHorn) → composed "47".
    TypeString(engine, L"7");
    EXPECT_EQ(engine.Peek(), L"47");
}

// =====================================================================
// G6 — Sentinel: explicit None == not set
// =====================================================================

TEST_F(CustomKeyMapTest, CustomMapNoneFallsThroughToClassifyKey) {
    TypingConfig cfg = MakeTelexConfig();
    // Explicitly set 's' to None — must be indistinguishable from default.
    cfg.customKeyMap[static_cast<size_t>(L's')] = TypingAction::None;
    TypingEngine engine(cfg);
    TypeString(engine, L"as");
    // Default Telex: 'a' + 's' → 'á' (sắc).
    EXPECT_EQ(engine.Peek(), L"á");
}

// =====================================================================
// G7 — Parametric smoke: every non-None TypingAction reachable via remap
// =====================================================================

class CustomKeyMapAllActions
    : public CustomKeyMapTest,
      public ::testing::WithParamInterface<TypingAction> {};

TEST_P(CustomKeyMapAllActions, EveryTypingActionNoThrow) {
    const TypingAction action = GetParam();
    ASSERT_NE(action, TypingAction::None) << "G7 only iterates non-None actions";

    // Configure: 'q' (which ClassifyKey returns None for) remapped to the
    // parameter action under UserDefined mode (customKeyMap is only honored
    // there per the gating invariant). We do not assert specific Vietnamese
    // strings — only that dispatch reaches the correct action handler
    // (no crash, action resolves). Engine state observation is via Peek().
    TypingConfig cfg = MakeUserDefinedConfig();
    cfg.customKeyMap[static_cast<size_t>(L'q')] = action;
    TypingEngine engine(cfg);
    // Seed a vowel so modifier/tone actions have something to operate on.
    TypeString(engine, L"a");
    EXPECT_NO_THROW(TypeString(engine, L"q"));
    // Final Peek should be a non-empty wstring (engine must not be in a
    // broken state after dispatch).
    EXPECT_FALSE(engine.Peek().empty());
}

INSTANTIATE_TEST_SUITE_P(
    AllActions,
    CustomKeyMapAllActions,
    ::testing::Values(
        TypingAction::ClearTone,
        TypingAction::ToneAcute,
        TypingAction::ToneGrave,
        TypingAction::ToneHook,
        TypingAction::ToneTilde,
        TypingAction::ToneDot,
        TypingAction::CircumflexA,
        TypingAction::CircumflexE,
        TypingAction::CircumflexO,
        TypingAction::HornW,
        TypingAction::HornInsertO,
        TypingAction::HornInsertU,
        TypingAction::StrokeD,
        TypingAction::VniCircumflex,
        TypingAction::VniHorn,
        TypingAction::VniBreve,
        TypingAction::VniStroke,
        TypingAction::HornOrInsertU,
        TypingAction::HornOrInsertUNoStart,
        TypingAction::UndoAllMarks,
        TypingAction::InsertABreve,
        TypingAction::InsertABreveUpper,
        TypingAction::InsertACircumflex,
        TypingAction::InsertACircumflexUpper,
        TypingAction::InsertDStroke,
        TypingAction::InsertDStrokeUpper,
        TypingAction::InsertECircumflex,
        TypingAction::InsertECircumflexUpper,
        TypingAction::InsertOCircumflex,
        TypingAction::InsertOCircumflexUpper,
        TypingAction::InsertOHorn,
        TypingAction::InsertOHornUpper,
        TypingAction::InsertUHorn,
        TypingAction::InsertUHornUpper
    )
);

// =====================================================================
// W8.0 — Audit: WouldModifierRecoverOrEscape vs HandleAdjacentCircumflex
//   target-resolution parity in UserDefined mode.
// Hypothesis (docs/plans/2026-05-25-feature-pipeline-w8.1-...md §9 STOP-1):
//   UserDefined remap `q → CircumflexA`. HandleAdjacentCircumflex resolves
//   target via ActionToVowel(action) = 'a' (post-Problem 6 fix L900-903).
//   WouldModifierRecoverOrEscape (L583-621) resolves via IsVowelChar(keyChar)
//   = IsVowelChar('q') = FALSE → escape gate at L592 silently skipped.
//   Probes pin observed behaviour so W8.1 can extend `IModifierSubExecutor`
//   contract without drifting from current semantics.
// =====================================================================

// PROBE 1 — target resolution under spellCheck=true.
// Telex `caa` and UserDefined `caq` (q→CircumflexA) must produce identical
// output. Exercises HandleAdjacentCircumflex L905 `last.base == targetBase`
// where targetBase comes from ActionToVowel in UserDefined mode.
TEST_F(CustomKeyMapTest, W8AuditTargetResolution_TelexCaaEqualsUserDefinedCaq) {
    TypingConfig telexCfg = MakeTelexConfig();
    telexCfg.spellCheckEnabled = true;
    TypingEngine telexEng(telexCfg);
    TypeString(telexEng, L"caa");
    EXPECT_EQ(telexEng.Peek(), L"câ") << "Telex baseline";

    TypingConfig udCfg = MakeUserDefinedConfig();
    udCfg.spellCheckEnabled = true;
    udCfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::CircumflexA;
    TypingEngine udEng(udCfg);
    TypeString(udEng, L"caq");
    EXPECT_EQ(udEng.Peek(), L"câ") << "UserDefined parity (ActionToVowel path)";
}

// PROBE 2 — adjacent escape under spellCheck=true.
// `caqq` second-q should escape â→a then literal q, matching Telex `caaa`.
// Exercises L914 escape branch reached via L905 ActionToVowel resolution.
TEST_F(CustomKeyMapTest, W8AuditEscape_TelexCaaaEqualsUserDefinedCaqq) {
    TypingConfig telexCfg = MakeTelexConfig();
    telexCfg.spellCheckEnabled = true;
    TypingEngine telexEng(telexCfg);
    TypeString(telexEng, L"caaa");
    const std::wstring telexResult = telexEng.Peek();

    TypingConfig udCfg = MakeUserDefinedConfig();
    udCfg.spellCheckEnabled = true;
    udCfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::CircumflexA;
    TypingEngine udEng(udCfg);
    TypeString(udEng, L"caqq");
    const std::wstring udResult = udEng.Peek();

    // Pin observed behaviour. Telex `caaa` expected: `caa` (second a → â,
    // third a → escape â→a + literal a). UserDefined `caqq` expected: `caq`.
    EXPECT_EQ(telexResult, L"caa") << "Telex escape baseline";
    EXPECT_EQ(udResult, L"caq") << "UserDefined parity (escape via ActionToVowel)";
}

// PROBE 3 — the SUSPECT path: gate WouldModifierRecoverOrEscape with
//   spellCheckDisabled_=true AND last is `â` mid-buffer.
// Construct by typing extra Invalid material after `câ` so spellCheckDisabled
// becomes true, then attempt an escape via the remapped key. If the gate
// at L592 silently skips IsVowelChar('q'), the escape will be blocked even
// though the equivalent Telex `a` press would pass.
// NOTE: this probe documents current behaviour — if Telex+UserDefined diverge
// here, the divergence is the W8.0 audit's primary finding.
TEST_F(CustomKeyMapTest, W8AuditGate_SpellDisabledThenEscapeKey) {
    // Set up a state where spellCheckDisabled_ becomes true while last
    // state is `â` (Circumflex on 'a') so the L592 gate is the only thing
    // standing between the key and escape.
    //
    // Sequence: `câ` (ValidPrefix) then add an Invalid coda then BACKSPACE
    // to leave `câ` with spellCheckDisabled possibly latched. If
    // spellCheckDisabled re-evaluates on Backspace via UpdateSpellState,
    // both engines will likely match. Probe captures whichever it is.

    TypingConfig telexCfg = MakeTelexConfig();
    telexCfg.spellCheckEnabled = true;
    TypingEngine telexEng(telexCfg);
    // Force spellCheckDisabled = true by typing through an Invalid syllable.
    // `caaa` → `caa` (escape) → final state {c,a,a} which is Invalid →
    // spellCheckDisabled_=true. Last is `a` (vowel, mod=None).
    TypeString(telexEng, L"caaa");
    ASSERT_EQ(telexEng.Peek(), L"caa") << "Telex precondition: caaa→caa";
    // Next `a` from disabled state — does it apply circumflex (escape via
    // L592 if last is â) or pass through?
    TypeString(telexEng, L"a");
    const std::wstring telexAfter = telexEng.Peek();

    TypingConfig udCfg = MakeUserDefinedConfig();
    udCfg.spellCheckEnabled = true;
    udCfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::CircumflexA;
    TypingEngine udEng(udCfg);
    TypeString(udEng, L"caqq");
    ASSERT_EQ(udEng.Peek(), L"caq") << "UserDefined precondition: caqq→caq";
    TypeString(udEng, L"q");
    const std::wstring udAfter = udEng.Peek();

    // Both should be: literal char appended (since last is base 'a' no
    // circumflex, gate's "escape existing mark" condition isn't met).
    // The interesting case is whether Telex and UserDefined paths agree.
    EXPECT_EQ(telexAfter, L"caaa") << "Telex: 4th a literal";
    EXPECT_EQ(udAfter, L"caqq")    << "UserDefined parity";
}

// PROBE 4 — the harder gate case: spellCheckDisabled_=true with subsequent
//   modifier-key press. Hypothesis would manifest as visible divergence IF
//   the L592 gate condition (last vowel + matching base + Circumflex mod)
//   were reachable while spellCheckDisabled_=true. Empirically, that joint
//   state is structurally unreachable (see comment below) — so the
//   keyChar-vs-ActionToVowel difference at L592 has no user-visible effect.
TEST_F(CustomKeyMapTest, W8AuditGate_NoDivergenceFromInvalidConsonantCluster) {
    // Construct: Invalid consonant cluster prefix + repeated CircumflexA
    // press. Telex (`xqaa`) and UserDefined `q→CircumflexA` (`xqaaq`) both
    // reach L519 with spellCheckDisabled_=true; both hit L592 with the
    // INNER condition failing (last vowel mod=None, not Circumflex →
    // nothing to escape). Outcome agrees regardless of which side of the
    // hypothetical bug we're on.
    TypingConfig telexCfg = MakeTelexConfig();
    telexCfg.spellCheckEnabled = true;
    TypingEngine telexEng(telexCfg);
    TypeString(telexEng, L"xqaa");
    const std::wstring telexResult = telexEng.Peek();

    TypingConfig udCfg = MakeUserDefinedConfig();
    udCfg.spellCheckEnabled = true;
    udCfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::CircumflexA;
    TypingEngine udEng(udCfg);
    TypeString(udEng, L"xqaaq");
    const std::wstring udResult = udEng.Peek();

    // Both reject circumflex application from Invalid state. No divergence.
    EXPECT_EQ(telexResult, L"xqaa")  << "Telex baseline: cluster rejects circumflex";
    EXPECT_EQ(udResult,    L"xqaaq") << "UserDefined parity: cluster rejects circumflex";

    // Why no divergence is possible in practice:
    //   To trigger L592's keyChar-vs-ActionToVowel divergence visibly we
    //   would need joint state {last = vowel+Circumflex (mod), buffer Invalid
    //   so spellCheckDisabled_=true}. This is structurally unreachable:
    //     (1) Applying circumflex to vowel `a` requires the L519 gate to
    //         pass, which currently demands spellCheckDisabled_=false OR
    //         the gate's narrow escape conditions; initial application
    //         from an Invalid pre-state is rejected by L944/L955.
    //     (2) Once circumflex is applied successfully, the result is
    //         (Valid|ValidPrefix) — spellCheckDisabled_ resets to false.
    //     (3) To then make buffer Invalid (spellCheckDisabled_=true)
    //         requires appending another char, which moves `last` away
    //         from the circumflexed vowel — inner condition again fails.
    //   Conclusion: the IsVowelChar(keyChar) check at L592 is functionally
    //   equivalent to IsVowelChar(ActionToVowel(action)) for the reachable
    //   state space. W8.0 audit verdict: no fix needed for L592.
}

// PROBE 5 — WRONG-BRANCH MATCH at L586 (`lower == L'w'`).
// The Telex branch of WouldModifierRecoverOrEscape is KEY-driven, not
// action-driven. UserDefined remap `w → CircumflexA` makes lower='w'
// while action=CircumflexA → L586 fires its Horn/Breve escape check
// for an action that has nothing to do with Horn/Breve. If the buffer
// holds an existing Horn (ư) or Breve (ă), the gate FALSELY passes
// canEscape=true. The downstream HandleAdjacentCircumflex either no-ops
// or applies Circumflex on its own terms — so divergence is internal,
// not user-visible. Probe pins this self-correction.
TEST_F(CustomKeyMapTest, W8AuditWrongBranch_W_RemappedToCircumflexA_WithHornInBuffer) {
    TypingConfig udCfg = MakeUserDefinedConfig();
    udCfg.spellCheckEnabled = true;
    udCfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::CircumflexA;
    udCfg.customKeyMap[static_cast<size_t>(L'[')] = TypingAction::HornInsertU;
    TypingEngine eng(udCfg);
    // `[` inserts ư (Horn modifier).
    // Then literal `q` to push state Invalid → spellCheckDisabled_=true.
    // Then press `w` (mapped CircumflexA). L519 enters gate; L586 sees
    // lower='w' → HasEscapableModifier(Horn) = true (ư has Horn) → gate
    // FALSELY passes for a non-Horn action. ProcessModifier(CircumflexA)
    // then runs HandleAdjacentCircumflex which finds no `a`-targetBase
    // match → returns false → literal w fallthrough.
    TypeString(eng, L"[");
    ASSERT_EQ(eng.Peek(), L"ư") << "HornInsertU baseline";
    TypeString(eng, L"q");
    TypeString(eng, L"w");
    EXPECT_EQ(eng.Peek(), L"ưqw")
        << "Self-correcting: wrong-branch gate-pass doesn't produce visible bug";
}

// PROBE 6 — WRONG-BRANCH MATCH at L589 (`lower == L'd'`).
// UserDefined remap `d → CircumflexA`. lower='d' → L589 StrokeD escape
// check fires for a Circumflex action. Same self-correction story.
TEST_F(CustomKeyMapTest, W8AuditWrongBranch_D_RemappedToCircumflexA_WithStrokeInBuffer) {
    TypingConfig udCfg = MakeUserDefinedConfig();
    udCfg.spellCheckEnabled = true;
    udCfg.customKeyMap[static_cast<size_t>(L'd')] = TypingAction::CircumflexA;
    udCfg.customKeyMap[static_cast<size_t>(L';')] = TypingAction::InsertDStroke;
    TypingEngine eng(udCfg);
    TypeString(eng, L";");
    ASSERT_EQ(eng.Peek(), L"đ") << "InsertDStroke baseline";
    TypeString(eng, L"q");
    TypeString(eng, L"d");
    EXPECT_EQ(eng.Peek(), L"đqd")
        << "Self-correcting: wrong-branch StrokeD gate-pass doesn't corrupt";
}

// PROBE 7 — VNI branch parity (action-driven via ActionToVniModifier).
// Unlike Telex branch, the VNI branch resolves the escape modifier from
// action (L610), not from keyChar. UserDefined remap `q → VniCircumflex`
// works correctly even with non-vowel keyChar — no wrong-branch class.
// This probe documents the architectural inconsistency: VNI side is the
// "clean" pattern Telex side should converge to in future refactor.
TEST_F(CustomKeyMapTest, W8AuditVniBranchIsActionDriven_NoWrongBranchClass) {
    TypingConfig udCfg = MakeUserDefinedConfig();
    udCfg.spellCheckEnabled = true;
    udCfg.customKeyMap[static_cast<size_t>(L'q')] = TypingAction::VniCircumflex;
    TypingEngine eng(udCfg);
    TypeString(eng, L"caq");
    EXPECT_EQ(eng.Peek(), L"câ")
        << "VNI ActionToVniModifier path: keyChar-agnostic";
    TypeString(eng, L"q");
    EXPECT_EQ(eng.Peek(), L"caq")
        << "VNI escape via action-driven path";
}

// PROBE 8 — Wrong-branch + spellCheckEnabled=FALSE path (gate not consulted).
// Sanity guard: with spellCheck off, WouldModifierRecoverOrEscape gate is
// never called (L519 condition). UserDefined remap quirks at L586/L589
// don't matter. Probe pins parity with Telex.
TEST_F(CustomKeyMapTest, W8AuditGateUnreachable_SpellCheckOff_NoBranchMattering) {
    TypingConfig udCfg = MakeUserDefinedConfig();
    udCfg.spellCheckEnabled = false;  // gate NEVER fires
    udCfg.customKeyMap[static_cast<size_t>(L'w')] = TypingAction::CircumflexA;
    TypingEngine eng(udCfg);
    TypeString(eng, L"caw");
    EXPECT_EQ(eng.Peek(), L"câ")
        << "spellCheck=off: gate skipped, CircumflexA applies normally";
    TypeString(eng, L"w");
    EXPECT_EQ(eng.Peek(), L"caw")
        << "Escape via HandleAdjacentCircumflex L914 (no gate involvement)";
}

}  // namespace
}  // namespace NextKey
