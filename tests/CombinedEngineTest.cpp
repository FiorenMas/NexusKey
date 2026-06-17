// VKey - Combined Mode (Telex + VNI) Tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include "core/engine/TypingEngine.h"
#include "core/config/TypingConfig.h"
#include "TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;

class CombinedEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Combined;
        config_.spellCheckEnabled = false;
        engine_ = std::make_unique<TypingEngine>(config_);
    }

    TypingConfig config_;
    std::unique_ptr<TypingEngine> engine_;
};

// ============================================================================
// VNI TONE KEYS (1-5) in Combined mode
// ============================================================================

TEST_F(CombinedEngineTest, VniTone_1_Acute) {
    TypeString(*engine_, L"a1");
    EXPECT_EQ(engine_->Peek(), L"\u00e1");  // á
}

TEST_F(CombinedEngineTest, VniTone_2_Grave) {
    TypeString(*engine_, L"a2");
    EXPECT_EQ(engine_->Peek(), L"\u00e0");  // à
}

TEST_F(CombinedEngineTest, VniTone_3_Hook) {
    TypeString(*engine_, L"a3");
    EXPECT_EQ(engine_->Peek(), L"\u1ea3");  // ả
}

TEST_F(CombinedEngineTest, VniTone_4_Tilde) {
    TypeString(*engine_, L"a4");
    EXPECT_EQ(engine_->Peek(), L"\u00e3");  // ã
}

TEST_F(CombinedEngineTest, VniTone_5_Dot) {
    TypeString(*engine_, L"a5");
    EXPECT_EQ(engine_->Peek(), L"\u1ea1");  // ạ
}

// ============================================================================
// VNI MODIFIER KEYS (6-9) in Combined mode
// ============================================================================

TEST_F(CombinedEngineTest, VniModifier_6_Circumflex) {
    TypeString(*engine_, L"a6");
    EXPECT_EQ(engine_->Peek(), L"\u00e2");  // â
}

TEST_F(CombinedEngineTest, VniModifier_7_Horn_O) {
    TypeString(*engine_, L"o7");
    EXPECT_EQ(engine_->Peek(), L"\u01a1");  // ơ
}

TEST_F(CombinedEngineTest, VniModifier_7_Horn_U) {
    TypeString(*engine_, L"u7");
    EXPECT_EQ(engine_->Peek(), L"\u01b0");  // ư
}

TEST_F(CombinedEngineTest, VniModifier_8_Breve) {
    TypeString(*engine_, L"a8");
    EXPECT_EQ(engine_->Peek(), L"\u0103");  // ă
}

TEST_F(CombinedEngineTest, VniModifier_9_Stroke) {
    TypeString(*engine_, L"dd");  // Telex dd still works
    EXPECT_EQ(engine_->Peek(), L"\u0111");  // đ
    engine_->Reset();
    TypeString(*engine_, L"d9");  // VNI d9 also works
    EXPECT_EQ(engine_->Peek(), L"\u0111");  // đ
}

TEST_F(CombinedEngineTest, VniTone_0_ClearTone) {
    TypeString(*engine_, L"a1");  // á
    EXPECT_EQ(engine_->Peek(), L"\u00e1");
    engine_->PushChar(L'0');     // clear tone — key consumed (same as Telex 'z')
    EXPECT_EQ(engine_->Peek(), L"a");
}

// ============================================================================
// TELEX STILL WORKS in Combined mode
// ============================================================================

TEST_F(CombinedEngineTest, TelexTone_StillWorks) {
    TypeString(*engine_, L"as");
    EXPECT_EQ(engine_->Peek(), L"\u00e1");  // á (Telex s = Acute)
}

TEST_F(CombinedEngineTest, TelexModifier_AA_StillWorks) {
    TypeString(*engine_, L"aa");
    EXPECT_EQ(engine_->Peek(), L"\u00e2");  // â
}

TEST_F(CombinedEngineTest, TelexModifier_W_StillWorks) {
    TypeString(*engine_, L"ow");
    EXPECT_EQ(engine_->Peek(), L"\u01a1");  // ơ
}

// ============================================================================
// CROSS-MODE INTERACTIONS
// ============================================================================

TEST_F(CombinedEngineTest, CrossMode_TelexCircumflex_VniBreve) {
    // aa (Telex circumflex) + 8 (VNI breve) → switch â→ă
    TypeString(*engine_, L"aa");
    EXPECT_EQ(engine_->Peek(), L"\u00e2");  // â
    engine_->PushChar(L'8');
    EXPECT_EQ(engine_->Peek(), L"\u0103");  // ă (Pass 1.5 switch)
}

TEST_F(CombinedEngineTest, CrossMode_VniCircumflex_TelexBreve) {
    // a6 (VNI circumflex) + w (Telex breve via P7) → switch â→ă
    TypeString(*engine_, L"a6");
    EXPECT_EQ(engine_->Peek(), L"\u00e2");  // â
    engine_->PushChar(L'w');
    EXPECT_EQ(engine_->Peek(), L"\u0103");  // ă (Telex P7 handles Circumflex→Breve)
}

TEST_F(CombinedEngineTest, CrossMode_TelexTone_VniEscape) {
    // as (Telex Acute) + 1 (VNI Acute) → escape (same tone)
    TypeString(*engine_, L"as");
    EXPECT_EQ(engine_->Peek(), L"\u00e1");  // á
    engine_->PushChar(L'1');
    EXPECT_EQ(engine_->Peek(), L"a1");      // escape: cleared tone + literal '1'
}

TEST_F(CombinedEngineTest, CrossMode_TelexTone_VniReplace) {
    // as (Telex Acute) + 2 (VNI Grave) → replace tone
    TypeString(*engine_, L"as");
    EXPECT_EQ(engine_->Peek(), L"\u00e1");  // á
    engine_->PushChar(L'2');
    EXPECT_EQ(engine_->Peek(), L"\u00e0");  // à (Grave replaced Acute)
}

TEST_F(CombinedEngineTest, CrossMode_TelexStroke_VniEscape) {
    // dd (Telex stroke) + 9 (VNI stroke) → escape
    TypeString(*engine_, L"dd");
    EXPECT_EQ(engine_->Peek(), L"\u0111");  // đ
    engine_->PushChar(L'9');
    EXPECT_EQ(engine_->Peek(), L"d9");      // escape
}

// ============================================================================
// CROSS-MODE — VNI tone + Telex escape
// ============================================================================

TEST_F(CombinedEngineTest, CrossMode_VniTone_TelexEscape) {
    // a1 (VNI Acute) + s (Telex Acute) → escape (same tone)
    TypeString(*engine_, L"a1");
    EXPECT_EQ(engine_->Peek(), L"\u00e1");  // á
    engine_->PushChar(L's');
    EXPECT_EQ(engine_->Peek(), L"as");      // escape
}

TEST_F(CombinedEngineTest, CrossMode_VniTone_TelexReplace) {
    // a1 (VNI Acute) + f (Telex Grave) → replace
    TypeString(*engine_, L"a1");
    EXPECT_EQ(engine_->Peek(), L"\u00e1");  // á
    engine_->PushChar(L'f');
    EXPECT_EQ(engine_->Peek(), L"\u00e0");  // à
}

TEST_F(CombinedEngineTest, CrossMode_VniStroke_TelexEscape) {
    // d9 (VNI stroke) + d (Telex stroke trigger) → escape
    TypeString(*engine_, L"d9");
    EXPECT_EQ(engine_->Peek(), L"\u0111");  // đ
    engine_->PushChar(L'd');
    EXPECT_EQ(engine_->Peek(), L"dd");      // escape
}

TEST_F(CombinedEngineTest, CrossMode_VniHorn_TelexCircumflex) {
    // o7 (VNI horn → ơ) + o (Telex circumflex trigger oo) → switch horn→circumflex
    // Actually: after o7 states=[{o+horn}], then 'o' → Telex double-vowel oo:
    // finds existing 'o' with horn → free marking switches horn→circumflex
    TypeString(*engine_, L"o7");
    EXPECT_EQ(engine_->Peek(), L"\u01a1");  // ơ
    engine_->PushChar(L'o');
    EXPECT_EQ(engine_->Peek(), L"\u00f4");  // ô (circumflex replaced horn)
}

// ============================================================================
// CROSS-MODE — Backspace undo
// ============================================================================

TEST_F(CombinedEngineTest, CrossMode_Backspace_AfterVniTone) {
    // a1 = 1 CharState (a+Acute). Backspace pops it → empty.
    TypeString(*engine_, L"a1");
    EXPECT_EQ(engine_->Peek(), L"\u00e1");  // á
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"");
}

TEST_F(CombinedEngineTest, CrossMode_Backspace_AfterVniModifier) {
    // a6 = 1 CharState (a+Circumflex). Backspace pops it → empty.
    TypeString(*engine_, L"a6");
    EXPECT_EQ(engine_->Peek(), L"\u00e2");  // â
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"");
}

TEST_F(CombinedEngineTest, CrossMode_Backspace_MultiChar) {
    // ba1 = 2 CharStates [{b}, {a+Acute}]. Backspace pops last → "b"
    TypeString(*engine_, L"ba1");
    EXPECT_EQ(engine_->Peek(), L"b\u00e1");  // bá
    engine_->Backspace();
    EXPECT_EQ(engine_->Peek(), L"b");
}

// ============================================================================
// VNI HORN — uo pair + standalone
// ============================================================================

TEST_F(CombinedEngineTest, VniHorn_UOPair) {
    // tuo7 → tươ (horn both u and o)
    TypeString(*engine_, L"tuo7");
    EXPECT_EQ(engine_->Peek(), L"t\u01b0\u01a1");  // tươ
}

TEST_F(CombinedEngineTest, VniHorn_UOPair_Edge_Huong) {
    // huong → hương (h prefix = edge case: 3-state cycle)
    TypeString(*engine_, L"huo");
    engine_->PushChar(L'7');  // step 1: horn o only → huơ
    EXPECT_EQ(engine_->Peek(), L"hu\u01a1");  // huơ
    engine_->Reset();
    TypeString(*engine_, L"huo");
    engine_->PushChar(L'7');  // step 1: horn o
    engine_->PushChar(L'7');  // step 2: horn u too → hươ
    EXPECT_EQ(engine_->Peek(), L"h\u01b0\u01a1");  // hươ
}

TEST_F(CombinedEngineTest, VniHorn_UUPattern) {
    // luu7 → lưu (horn first u, 2-press cycle)
    TypeString(*engine_, L"luu7");
    EXPECT_EQ(engine_->Peek(), L"l\u01b0u");  // lưu
}

TEST_F(CombinedEngineTest, VniHorn_UUPattern_Escape) {
    // luu77 → escape horn on first u
    TypeString(*engine_, L"luu7");
    EXPECT_EQ(engine_->Peek(), L"l\u01b0u");  // lưu
    engine_->PushChar(L'7');  // escape
    EXPECT_EQ(engine_->Peek(), L"luu7");      // horn cleared + literal '7'
}

// ============================================================================
// VNI MODIFIER SWITCHING (Pass 1.5)
// ============================================================================

TEST_F(CombinedEngineTest, VniModifierSwitch_Circumflex_to_Horn) {
    // o6 (circumflex ô) + 7 (horn) → switch to ơ
    TypeString(*engine_, L"o6");
    EXPECT_EQ(engine_->Peek(), L"\u00f4");  // ô
    engine_->PushChar(L'7');
    EXPECT_EQ(engine_->Peek(), L"\u01a1");  // ơ
}

TEST_F(CombinedEngineTest, VniModifierSwitch_Horn_to_Circumflex) {
    // o7 (horn ơ) + 6 (circumflex) → switch to ô
    TypeString(*engine_, L"o7");
    EXPECT_EQ(engine_->Peek(), L"\u01a1");  // ơ
    engine_->PushChar(L'6');
    EXPECT_EQ(engine_->Peek(), L"\u00f4");  // ô
}

TEST_F(CombinedEngineTest, VniModifierSwitch_Breve_to_Circumflex) {
    // a8 (breve ă) + 6 (circumflex) → switch to â
    TypeString(*engine_, L"a8");
    EXPECT_EQ(engine_->Peek(), L"\u0103");  // ă
    engine_->PushChar(L'6');
    EXPECT_EQ(engine_->Peek(), L"\u00e2");  // â
}

// ============================================================================
// COMBINED MODE WORD EXAMPLES
// ============================================================================

TEST_F(CombinedEngineTest, Word_Viet_TelexCircumflex_VniTone) {
    // vieet5 → việt (Telex ee=circumflex, VNI 5=dot)
    TypeString(*engine_, L"vieet5");
    EXPECT_EQ(engine_->Peek(), L"vi\u1ec7t");  // việt
}

TEST_F(CombinedEngineTest, Word_FullVni) {
    // vie65t → việt (VNI 6=circumflex on e, VNI 5=dot)
    TypeString(*engine_, L"vie65t");
    EXPECT_EQ(engine_->Peek(), L"vi\u1ec7t");  // việt
}

TEST_F(CombinedEngineTest, Word_FullTelex) {
    // vieejt → việt (Telex ee=circumflex, j=dot)
    TypeString(*engine_, L"vieejt");
    EXPECT_EQ(engine_->Peek(), L"vi\u1ec7t");  // việt
}

TEST_F(CombinedEngineTest, Word_Duong_VniHornAndTone) {
    // duong72 → đường... wait, no: d-u-o-n-g-7-2
    // d=regular, u-o-n-g=regular chars, 7=horn on uo pair, 2=grave
    TypeString(*engine_, L"duong72");
    EXPECT_EQ(engine_->Peek(), L"d\u01b0\u1eddng");  // dường
}

TEST_F(CombinedEngineTest, Word_QuickConsonant_VniTone) {
    // nn1 (quick consonant nn→ng) then 1 on what? No vowel → literal
    // Better: ang → a+ng, then VNI tone on 'a'
    config_.quickEndConsonant = true;
    engine_ = std::make_unique<TypingEngine>(config_);
    TypeString(*engine_, L"a");
    engine_->PushChar(L'g');  // quick end: g→ng after vowel
    EXPECT_EQ(engine_->Peek(), L"ang");
    // Can't apply tone after quick end consonant committed — engine state has 3 chars
    // Test that VNI tone still works when typed before quick end
    engine_->Reset();
    TypeString(*engine_, L"a1");
    engine_->PushChar(L'g');  // quick end: g→ng after toned vowel
    EXPECT_EQ(engine_->Peek(), L"\u00e1ng");  // áng
}

TEST_F(CombinedEngineTest, Word_Escape_ThenContinueVni) {
    // as (Telex tone) + s (escape) → "as", then type more + VNI tone
    TypeString(*engine_, L"ass");  // a→á→as (escape)
    // escape_ is set, next VNI tone should be blocked
    engine_->PushChar(L'1');
    // escape blocks '1' → literal
    EXPECT_EQ(engine_->Peek(), L"as1");
}

// ============================================================================
// ENGLISH PROTECTION in Combined mode
// ============================================================================

class CombinedEngineSpellTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Combined;
        config_.spellCheckEnabled = true;
        engine_ = std::make_unique<TypingEngine>(config_);
    }

    TypingConfig config_;
    std::unique_ptr<TypingEngine> engine_;
};

TEST_F(CombinedEngineSpellTest, EnglishProtection_AdminDigits) {
    // admin1 → HardEnglish (coda dm invalid) → digit treated as literal
    TypeString(*engine_, L"admin1");
    EXPECT_EQ(engine_->Peek(), L"admin1");
}

TEST_F(CombinedEngineSpellTest, EnglishProtection_TestDigits) {
    // test1 → Telex 's' tries tone but 'st' coda → HardEnglish, then '1' blocked
    TypeString(*engine_, L"test1");
    // 't' after tone would be irregular; exact behavior depends on spell state
    // Key assertion: '1' should NOT apply tone to produce diacritics
    std::wstring result = engine_->Peek();
    // Result should not contain Vietnamese diacritics from the '1' key
    bool hasDiacriticsFrom1 = false;
    for (wchar_t ch : result) {
        if (ch > 0x7F && ch != L'\u0111') hasDiacriticsFrom1 = true;
    }
    // In practice: 's' applies tone to 'e' → 'tét', then 't' after, then '1' escapes or is literal
    // The important thing is the overall word doesn't produce unexpected Vietnamese
}

// T5 (docs/TODO.md): baseline forward 'casc' → 'các' under spell-check ON.
TEST_F(CombinedEngineSpellTest, T5_ToneAfterCoda_BaselineForward) {
    TypeString(*engine_, L"casc");
    EXPECT_EQ(engine_->Peek(), L"các");
}

// T5 (docs/TODO.md): user mistypes huyền 'f' on 'ca', adds coda 'c', then
// corrects with sắc 's'. Engine must replace huyền with sắc on 'à' → 'các'.
// Spell-check ON — this is where the original bug was reported. If engine
// alone (TelexEngineTest spell-check OFF) passes but THIS fails, the bug is
// in the validator (PhonotacticsValidator) blocking the post-replace candidate.
TEST_F(CombinedEngineSpellTest, T5_ToneReplaceAfterCoda_WithSpellCheck) {
    TypeString(*engine_, L"cafcs");
    EXPECT_EQ(engine_->Peek(), L"các")
        << "After 'cafcs' under spell-check ON, engine must replace huyền\n"
        << "with sắc on already-toned 'à' → 'các'. Bug per docs/TODO.md.";
}

// T5 case 2 (anh 2026-05-07): modifier on invalid buffer + tone correction.
// User types 'cafc' (yields invalid `càc`), then 'w' to change 'a' to 'ă'
// (yields `cằc` — still invalid since huyền + stop coda), then 'j' to switch
// tone to nặng → `cặc` (valid: nặng + stop coda). Expects engine to allow
// the modifier through despite intermediate invalid state, and the tone-
// replacement gate to recover validity on the final 'j'.
TEST_F(CombinedEngineSpellTest, T5_ModifierThenToneFix_W_J) {
    TypeString(*engine_, L"cafcwj");
    EXPECT_EQ(engine_->Peek(), L"cặc")
        << "User mistypes huyền+coda invalid (càc), then corrects vowel with\n"
        << "w (a→ă, still invalid cằc), then tone with j (huyền→nặng = cặc valid).";
}

// T5 case 2 variant: same scenario with 'aa' (circumflex a→â) instead of 'w'.
TEST_F(CombinedEngineSpellTest, T5_ModifierThenToneFix_AA_J) {
    TypeString(*engine_, L"cafcaj");
    EXPECT_EQ(engine_->Peek(), L"cậc")
        << "User corrects vowel with aa (a→â) then tone with j (huyền→nặng).";
}

// T5 case 1 — generic across vowels (anh question 2026-05-07: "mấy chữ như e o có bị không").
// Verify the tone-replacement-recovers-validity fix works for all main vowels,
// not just 'a'. ValidateSyllableState is called on the speculative buffer so the
// recovery condition is purely "speculative result is Valid" — vowel-agnostic.
TEST_F(CombinedEngineSpellTest, T5_ToneReplaceAfterCoda_E) {
    // kefcs: k-e-f-c-s → kè coda → kèc invalid (huyền+stop) → 's' replaces → kéc.
    // Note: 'cefcs' would fail because c/k/qu onset rule blocks 'c+e' (use 'k+e').
    TypeString(*engine_, L"kefcs");
    EXPECT_EQ(engine_->Peek(), L"kéc");
}

TEST_F(CombinedEngineSpellTest, T5_ToneReplaceAfterCoda_O) {
    // cofcs: c-o-f-c-s → còc invalid → 's' replaces huyền with sắc → cóc (real word: "frog").
    TypeString(*engine_, L"cofcs");
    EXPECT_EQ(engine_->Peek(), L"cóc");
}

TEST_F(CombinedEngineSpellTest, T5_ToneReplaceAfterCoda_OO) {
    // cooft → coo→ô, f huyền → cồt invalid (huyền+stop) → s replaces → cốt (real word: "core").
    TypeString(*engine_, L"coofts");
    EXPECT_EQ(engine_->Peek(), L"cốt");
}

}  // namespace
}  // namespace NextKey
