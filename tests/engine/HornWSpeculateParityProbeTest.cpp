// tests/engine/HornWSpeculateParityProbeTest.cpp
//
// W8.2 — probes for the hypothetical Horn P5/P6 speculate-relocate mismatch
// (TODO 2026-05-23). Per `feedback_defer_with_promise` + `feedback_probe_before_theorize`,
// no concrete user repro exists for the structural symmetry between Horn
// P5/P6 (validates mod-only, applies with RelocateToneToHornVowel) and the
// free-marking circumflex bug fixed by ad09f15.
//
// VERDICT after running 8 probes (2026-05-25):
//   No mismatch surface found. P5/P6 inherently fire only for STANDALONE
//   u/o (no pair) — with a single horned vowel, RelocateToneToHornVowel
//   has nothing to relocate (tone stays on its current vowel which IS
//   the horn vowel). Combined with the existing T5 tone-stop-coda
//   mismatch recovery (WouldBeValidSyllable L2085-2086), the mod-only
//   speculation reaches the same Valid/Invalid verdict as the apply
//   path would.
//
//   Probes stay as regression guards for the day a real user input
//   surfaces the mismatch. If any probe begins failing, that input is
//   the concrete repro the TODO 2026-05-23 was waiting for.
//
// Coverage matrix:
//   P5 (standalone u → horn):
//     - tufn+w → từn (clean accept, no coda issue)
//     - tups+w → tứp (clean accept with stop coda)
//     - tungf+w → từng (real VN word, validates)
//     - uaan+w → ưan (sister-circumflex strip path)
//     - tufnh+w → tùnhw (rejected: structural Invalid coda `unh`)
//     - tufp+w → từp (T5 recovery: grave+stop-p invalid is accepted via mid-correction)
//   P6 (standalone o → horn):
//     - tofn+w → tờn
//     - hojt+w → hợt (real VN word)
#include <gtest/gtest.h>

#include <string>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingEngine.h"
#include "../TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;

class HornWSpeculateParityProbe : public ::testing::Test {
protected:
    TypingConfig MakeTelexSpellOn() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Telex;
        cfg.spellCheckEnabled = true;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// PROBE 1 — P5: tone-grave on standalone u + coda 'n'.
// Mod-only validates `từn` (grave + n coda is valid). Apply path produces same.
TEST_F(HornWSpeculateParityProbe, P5_Tufn_GraveOnU_AcceptsHorn) {
    TypingEngine eng(MakeTelexSpellOn());
    TypeString(eng, L"tufnw");
    EXPECT_EQ(eng.Peek(), L"từn");
}

// PROBE 2 — P5: tone-acute on u + stop coda 'p'.
TEST_F(HornWSpeculateParityProbe, P5_Tups_AcuteOnU_AcceptsHorn) {
    TypingEngine eng(MakeTelexSpellOn());
    TypeString(eng, L"tupsw");
    EXPECT_EQ(eng.Peek(), L"tứp");
}

// PROBE 3 — P5: tone-grave on u + digraph coda 'ng' (real VN word `từng`).
TEST_F(HornWSpeculateParityProbe, P5_Tungf_GraveOnU_WithDigraph) {
    TypingEngine eng(MakeTelexSpellOn());
    TypeString(eng, L"tungfw");
    EXPECT_EQ(eng.Peek(), L"từng");
}

// PROBE 4 — P6: tone-grave on standalone o + coda 'n'.
TEST_F(HornWSpeculateParityProbe, P6_Tofn_GraveOnO_AcceptsHorn) {
    TypingEngine eng(MakeTelexSpellOn());
    TypeString(eng, L"tofnw");
    EXPECT_EQ(eng.Peek(), L"tờn");
}

// PROBE 5 — P6: tone-dot on o + stop coda 't' (real VN word `hợt`).
TEST_F(HornWSpeculateParityProbe, P6_Hojt_DotOnO_AcceptsHorn) {
    TypingEngine eng(MakeTelexSpellOn());
    TypeString(eng, L"hojtw");
    EXPECT_EQ(eng.Peek(), L"hợt");
}

// PROBE 6 — P5 with sister-circumflex strip (the L1259 comment example).
// `uaan+w`: second `a` applies CircumflexA on first a → states={u,â,n}.
// Then w → P5 standalone u, aIdx points to â. Apply strips â back to a
// and horns the u → `ưan`. Validate path also accepts.
TEST_F(HornWSpeculateParityProbe, P5_Uaan_SisterCircumflexStrip) {
    TypingEngine eng(MakeTelexSpellOn());
    TypeString(eng, L"uaanw");
    EXPECT_EQ(eng.Peek(), L"ưan");
}

// PROBE 7 — P5 rejected by structural Invalid: `unh` is not a valid VN
// coda. Both mod-only and apply-path would reject; pin behaviour.
TEST_F(HornWSpeculateParityProbe, P5_Tufnh_RejectedByStructuralInvalid) {
    TypingEngine eng(MakeTelexSpellOn());
    TypeString(eng, L"tufnhw");
    EXPECT_EQ(eng.Peek(), L"tùnhw")
        << "P5 rejected (unh not a valid coda); w falls to literal";
}

// PROBE 8 — P5 with grave + stop coda: T5 case 2 recovery accepts despite
// post-apply Invalid (tone-stop-coda mismatch is "mid-correction").
TEST_F(HornWSpeculateParityProbe, P5_Tufp_GraveStopCodaT5Recovery) {
    TypingEngine eng(MakeTelexSpellOn());
    TypeString(eng, L"tufpw");
    EXPECT_EQ(eng.Peek(), L"từp")
        << "T5 mid-correction: grave+p invalid accepted, next tone key recovers";
}

}  // namespace
}  // namespace NextKey
