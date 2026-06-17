// tests/engine/VniVowelModifierSpeculateParityProbeTest.cpp
//
// W8.5 — probe TODO 2026-05-25: ProcessVniVowelModifier has the same
// speculate-relocate invariant violation that ad09f15 + c6369dd fixed for
// Telex HandleAdjacentCircumflex. Pass 1 at TypingEngine.cpp ~L1949-1957
// validates mod-only via ShouldRejectModifier but the runtime calls
// RelocateToneToTarget after applying the modifier — same divergence.
//
// VNI parallels of the Telex repros:
//   - Telex `vijeet` (j=Dot, e=CircumflexE) ↔ VNI `vi5e6t` (5=Dot, 6=Circumflex)
//   - Telex `ngufoon` (f=Grave, o=CircumflexO) ↔ VNI `ngu2oo6n` (2=Grave, 6=Circumflex)
//
// If the engine produces the wrong output (raw + literal 6), this is the
// concrete repro the TODO 2026-05-25 was waiting for → W8.5 lands the
// ValidPrefix-gated fix. If outputs match the Telex-side expected
// (việt / nguồn), no fix needed → defer per TODO discipline.
#include <gtest/gtest.h>

#include <string>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingEngine.h"
#include "../TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;

class VniVowelModifierSpeculateParityProbe : public ::testing::Test {
protected:
    TypingConfig MakeVni() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::VNI;
        cfg.spellCheckEnabled = true;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// PROBE 1 — VNI mirror of Telex `vijeet → việt` (c6369dd).
// VNI: v-i-5(Dot on i)-e-6(CircumflexE)-t.
// Pre-state at modifier press: `{v,ị,e}` ValidPrefix (could become việt).
// If ProcessVniVowelModifier validates mod-only via ShouldRejectModifier
// without speculating RelocateToneToTarget, the validator sees the tone
// stranded on `i` of `iê` (which the rule says belongs on `ê`) → marks
// Invalid → wrong-reject → output stays uncomposed.
TEST_F(VniVowelModifierSpeculateParityProbe, VniVi5e6t_TonemidPromotesToViet) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"vi5e6t");
    EXPECT_EQ(eng.Peek(), L"việt")
        << "VNI mirror of Telex vijeet; if engine outputs raw + literal 6, "
        << "W8.5 must port the ValidPrefix-gated branch from c6369dd";
}

// PROBE 2 — VNI mirror of Telex `ngufoon → nguồn`.
// VNI: n-g-u-2(Grave on u)-o(literal)-6(CircumflexO)-n. SINGLE `o` because
// VNI's `6` is an explicit modifier — the Telex `oo` double-key is one
// literal `o` followed by `o` acting as CircumflexO. The TODO 2026-05-25
// transcription `ngu2oo6n` was incorrect — it would mean 2 literal o's
// in VNI, which is structurally invalid (3-vowel uoo). The valid VNI
// mirror uses single o.
TEST_F(VniVowelModifierSpeculateParityProbe, VniNgu2o6n_TonemidPromotesToNguon) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"ngu2o6n");
    EXPECT_EQ(eng.Peek(), L"nguồn")
        << "VNI mirror of Telex ngufoon (corrected sequence: single o + 6)";
}

// PROBE 3 — control: VNI `vi5et` (without the 6 modifier). Pin baseline.
TEST_F(VniVowelModifierSpeculateParityProbe, VniVi5et_NoModifier_StaysRaw) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"vi5et");
    // No circumflex applied; tone on i; final t coda. Output: vịet.
    EXPECT_EQ(eng.Peek(), L"vịet");
}

// PROBE 4 — control: VNI `viet6` (modifier AFTER everything). Pin baseline
// for the canonical "tone last" VNI path.
TEST_F(VniVowelModifierSpeculateParityProbe, VniViet6_ModifierAtEnd_AcceptsCircumflex) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"viet6");
    // Without tone: viet + 6 → viêt (no tone since no number key pressed).
    EXPECT_EQ(eng.Peek(), L"viêt");
}

}  // namespace
}  // namespace NextKey
