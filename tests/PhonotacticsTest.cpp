// VKey - Phonotactics Unit Tests
// SPDX-License-Identifier: AGPL-3.0-only
//
// Tests for IPhonotactics rule engine: tone position, syllable validity,
// completability. Operates on rendered Vietnamese text (wstring_view).

#include <gtest/gtest.h>
#include "core/engine/IPhonologyRules.h"
#include "core/engine/IPhonotactics.h"
#include "core/engine/DefaultPhonologyRules.h"
#include "core/engine/PhonologyRulePackFactory.h"
#include "core/engine/Phonotactics.h"
#include "core/engine/TypingEngine.h"
#include "core/config/TypingConfig.h"

namespace NextKey {
namespace Phonology {
namespace {

using ::testing::Test;

//=============================================================================
// TonePosition — locates index in vowelSeq where tone diacritic belongs
//=============================================================================

class PhonotacticsTonePosition : public ::testing::Test {
protected:
    Phonotactics phon_;
    static constexpr bool kClassic = false;
    static constexpr bool kModern  = true;
};

TEST_F(PhonotacticsTonePosition, EmptyVowelReturnsSizeMax) {
    EXPECT_EQ(phon_.TonePosition(L"", L"", kClassic), SIZE_MAX);
}

TEST_F(PhonotacticsTonePosition, SingleVowelReturnsZero) {
    EXPECT_EQ(phon_.TonePosition(L"a", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"e", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"i", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"o", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"u", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"y", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, SingleHornVowelReturnsZero) {
    // ư (U+01B0), ơ (U+01A1) — horn vowels alone → tone on themselves
    EXPECT_EQ(phon_.TonePosition(L"ư", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"ơ", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongAiToneOnFirst) {
    // "ai" (closed) — table rule 1 → first vowel: ái
    EXPECT_EQ(phon_.TonePosition(L"ai", L"", kClassic), 0u);
    EXPECT_EQ(phon_.TonePosition(L"ai", L"", kModern), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongAoToneOnFirst) {
    // "ao" (closed) — rule 1 → first: áo
    EXPECT_EQ(phon_.TonePosition(L"ao", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaClassicNoCodaToneOnFirst) {
    // "oa" classic, no coda → "hòa": tone on FIRST (rule 3 → first when no coda)
    EXPECT_EQ(phon_.TonePosition(L"oa", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaModernNoCodaToneOnSecond) {
    // "oa" modern, no coda → "hoà": tone on SECOND (rule 2 in modern table)
    EXPECT_EQ(phon_.TonePosition(L"oa", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, DiphthongOaClassicWithCodaToneOnSecond) {
    // "oa" classic, with coda → "hoàn": rule 3 with coda → SECOND
    EXPECT_EQ(phon_.TonePosition(L"oa", L"n", kClassic), 1u);
}

TEST_F(PhonotacticsTonePosition, HornDiphthongUOToneOnHorn) {
    // "ươ" (U+01B0 + U+01A1) — P1 horn priority, last horn = ơ
    EXPECT_EQ(phon_.TonePosition(L"ươ", L"", kClassic), 1u);
    EXPECT_EQ(phon_.TonePosition(L"ươ", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, ModifiedVowelGetsTonePriority) {
    // "ie" with ê (e+circumflex U+00EA) → P2 modified vowel: tone on ê
    EXPECT_EQ(phon_.TonePosition(L"iê", L"", kClassic), 1u);  // iê → tone on ê
    EXPECT_EQ(phon_.TonePosition(L"âu", L"", kClassic), 0u);  // âu → tone on â
}

TEST_F(PhonotacticsTonePosition, TriphthongOaiToneOnMiddle) {
    // "oai" modern → triphthong, tone on MIDDLE: hoài
    EXPECT_EQ(phon_.TonePosition(L"oai", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, TriphthongUyuToneOnMiddle) {
    // "uyu" modern → triphthong, tone on MIDDLE: khuỷu
    EXPECT_EQ(phon_.TonePosition(L"uyu", L"", kModern), 1u);
}

TEST_F(PhonotacticsTonePosition, ThreeVowelUyeToneOnModifiedThird) {
    // "uyê" — ê is modified (P2 priority) → tone on ê (index 2)
    // (RuleTiengViet exception: 3-vowel default is middle, except uyê)
    EXPECT_EQ(phon_.TonePosition(L"uyê", L"", kModern), 2u);
}

TEST_F(PhonotacticsTonePosition, ShiftedThreeVowelTypoAoi) {
    // "aoi" typo (gạo + extra i): shifted-3-vowel rule fires — first 2 vowels
    // (a,o) have a diphthong rule, so tone stays on the original diphthong's
    // FIRST vowel (a, index 0) instead of sliding to the typo's last-2 (o,i)
    // pair which would give index 1.
    EXPECT_EQ(phon_.TonePosition(L"aoi", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, ShiftedThreeVowelRepeatHoaa) {
    // "oaa" typo (hòa + extra a): shift fires onto first 2 (o,a) which is
    // rule-3 (coda-aware). Vowel-repeat at end overrides coda-aware to
    // FIRST → tone on 'o' (index 0).
    EXPECT_EQ(phon_.TonePosition(L"oaa", L"", kClassic), 0u);
}

TEST_F(PhonotacticsTonePosition, ClassicOaiHasRemainderRule) {
    // "oai" classic: shift fires onto (o,a) rule 3, but classic-mode "oai"
    // has more text (the trailing 'i') after the secondPos, so coda-aware
    // resolves to SECOND → tone on 'a' (index 1). Different from triphthong
    // path (modern) which also returns 1 but for a different reason.
    EXPECT_EQ(phon_.TonePosition(L"oai", L"", kClassic), 1u);
}

//=============================================================================
// IsValidSyllable — full syllable validity per RuleTiengViet
//=============================================================================

class PhonotacticsIsValidSyllable : public ::testing::Test {
protected:
    Phonotactics phon_;
    static constexpr bool kClassic = false;
    static constexpr bool kModern  = true;
};

TEST_F(PhonotacticsIsValidSyllable, SimpleConsonantVowelIsValid) {
    // "ba" — onset b, vowel a, no coda, no tone → valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, VowelInitialIsValid) {
    // "an" — no onset, vowel a, coda n → valid (e.g. "ăn", "an")
    EXPECT_TRUE(phon_.IsValidSyllable(L"", L"a", L"n", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, ClosedVowelRejectsCoda) {
    // "ai" is a closed vowel → cannot have coda. "ain" invalid.
    EXPECT_FALSE(phon_.IsValidSyllable(L"", L"ai", L"n", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, PendingVowelRequiresCoda) {
    // "ă" is pending (must have coda). "bă" alone invalid.
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"ă", L"", Tone::None, kModern));
    // "băn" valid.
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"ă", L"n", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, CodaCPRestrictsToneToAcuteOrDot) {
    // "bac" with sắc → bác valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"c", Tone::Acute, kModern));
    // "bac" with nặng → bạc valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"c", Tone::Dot, kModern));
    // "bac" with huyền → invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"a", L"c", Tone::Grave, kModern));
    // "bat" with hỏi → invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"a", L"t", Tone::Hook, kModern));
    // "bach" with sắc → bách valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"ch", Tone::Acute, kModern));
    // "bap" with ngã → invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"a", L"p", Tone::Tilde, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, OpenCodaAllowsAnyTone) {
    // "ban" with any tone → valid (n is not c/ch/p/t)
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Acute, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Grave, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Hook, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Tilde, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"n", Tone::Dot, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, OpenSyllableAllowsAnyTone) {
    // "ba" no coda → all tones valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Acute, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Grave, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Hook, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Tilde, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"", Tone::Dot, kModern));
}

//=============================================================================
// IsValidSyllable — onset / vowel front-back agreement.
// Rule and qu-exemption rationale documented at the helper definition in
// Phonotactics.cpp.
//=============================================================================

TEST_F(PhonotacticsIsValidSyllable, OnsetCAcceptsBackVowels) {
    // ca, cô, cu, cơ, cư, cân, căn — c + back vowel = valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"c", L"a",       L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"c", L"\x00F4",  L"",  Tone::None, kModern));   // cô
    EXPECT_TRUE(phon_.IsValidSyllable(L"c", L"u",       L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"c", L"\x01A1",  L"",  Tone::None, kModern));   // cơ
    EXPECT_TRUE(phon_.IsValidSyllable(L"c", L"\x01B0",  L"",  Tone::None, kModern));   // cư
    EXPECT_TRUE(phon_.IsValidSyllable(L"c", L"\x00E2",  L"n", Tone::None, kModern));   // cân
    EXPECT_TRUE(phon_.IsValidSyllable(L"c", L"\x0103",  L"n", Tone::None, kModern));   // căn
}

TEST_F(PhonotacticsIsValidSyllable, OnsetCRejectsFrontVowels) {
    // ce, cê, ci, cy — c + front vowel = invalid (must use k)
    EXPECT_FALSE(phon_.IsValidSyllable(L"c", L"e",       L"", Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"c", L"\x00EA",  L"", Tone::None, kModern));   // cê
    EXPECT_FALSE(phon_.IsValidSyllable(L"c", L"i",       L"", Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"c", L"y",       L"", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, OnsetKAcceptsFrontVowels) {
    // ke, kê, ki, ky, ken, kim, kênh — k + front vowel = valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"k", L"e",       L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"k", L"\x00EA",  L"",  Tone::None, kModern));   // kê
    EXPECT_TRUE(phon_.IsValidSyllable(L"k", L"i",       L"m", Tone::None, kModern));   // kim
    EXPECT_TRUE(phon_.IsValidSyllable(L"k", L"y",       L"",  Tone::None, kModern));   // ky
    EXPECT_TRUE(phon_.IsValidSyllable(L"k", L"e",       L"n", Tone::None, kModern));   // ken
    EXPECT_TRUE(phon_.IsValidSyllable(L"k", L"\x00EA",  L"nh", Tone::None, kModern));  // kênh
}

TEST_F(PhonotacticsIsValidSyllable, OnsetKRejectsBackVowels) {
    // ka, kô, ku, kơ, kư, kâu — k + back vowel = invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"k", L"a",       L"",  Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"k", L"\x00F4",  L"",  Tone::None, kModern));   // kô
    EXPECT_FALSE(phon_.IsValidSyllable(L"k", L"u",       L"",  Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"k", L"\x01A1",  L"",  Tone::None, kModern));   // kơ
    EXPECT_FALSE(phon_.IsValidSyllable(L"k", L"\x01B0",  L"",  Tone::None, kModern));   // kư
    EXPECT_FALSE(phon_.IsValidSyllable(L"k", L"\x00E2",  L"u", Tone::None, kModern));   // kâu
}

TEST_F(PhonotacticsIsValidSyllable, OnsetGAcceptsBackVowels) {
    // ga, gô, gu, gơ, gan — g + back vowel = valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"g", L"a",       L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"g", L"\x00F4",  L"",  Tone::None, kModern));   // gô
    EXPECT_TRUE(phon_.IsValidSyllable(L"g", L"u",       L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"g", L"a",       L"n", Tone::None, kModern));   // gan
    EXPECT_TRUE(phon_.IsValidSyllable(L"g", L"\x01A1",  L"i", Tone::None, kModern));   // gơi-ish
}

TEST_F(PhonotacticsIsValidSyllable, OnsetGRejectsFrontVowels) {
    // ge, gê, gi — g + front vowel = invalid (must use gh, or "gi" cluster onset)
    EXPECT_FALSE(phon_.IsValidSyllable(L"g", L"e",      L"", Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"g", L"\x00EA", L"", Tone::None, kModern));   // gê
    EXPECT_FALSE(phon_.IsValidSyllable(L"g", L"i",      L"", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, OnsetGhAcceptsFrontVowels) {
    // ghe, ghê, ghi, ghen — gh + front vowel = valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"gh", L"e",      L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"gh", L"\x00EA", L"",  Tone::None, kModern));  // ghê
    EXPECT_TRUE(phon_.IsValidSyllable(L"gh", L"i",      L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"gh", L"e",      L"n", Tone::None, kModern));  // ghen
}

TEST_F(PhonotacticsIsValidSyllable, OnsetGhRejectsBackVowels) {
    // gha, ghu, ghô — gh + back vowel = invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"gh", L"a",      L"", Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"gh", L"u",      L"", Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"gh", L"\x00F4", L"", Tone::None, kModern));  // ghô
}

TEST_F(PhonotacticsIsValidSyllable, OnsetNgAcceptsBackVowels) {
    // nga, ngô, ngu, ngư, ngon — ng + back = valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"ng", L"a",       L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"ng", L"\x00F4",  L"",  Tone::None, kModern));  // ngô
    EXPECT_TRUE(phon_.IsValidSyllable(L"ng", L"u",       L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"ng", L"\x01B0",  L"",  Tone::None, kModern));  // ngư
    EXPECT_TRUE(phon_.IsValidSyllable(L"ng", L"o",       L"n", Tone::None, kModern));  // ngon
}

TEST_F(PhonotacticsIsValidSyllable, OnsetNgRejectsFrontVowels) {
    // nge, ngê, ngi — ng + front = invalid (must use ngh)
    EXPECT_FALSE(phon_.IsValidSyllable(L"ng", L"e",      L"", Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"ng", L"\x00EA", L"", Tone::None, kModern));   // ngê
    EXPECT_FALSE(phon_.IsValidSyllable(L"ng", L"i",      L"", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, OnsetNghAcceptsFrontVowels) {
    // nghe, nghê, nghi, nghin — ngh + front = valid
    EXPECT_TRUE(phon_.IsValidSyllable(L"ngh", L"e",      L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"ngh", L"\x00EA", L"",  Tone::None, kModern));  // nghê
    EXPECT_TRUE(phon_.IsValidSyllable(L"ngh", L"i",      L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"ngh", L"i",      L"n", Tone::None, kModern));  // nghin
}

TEST_F(PhonotacticsIsValidSyllable, OnsetNghRejectsBackVowels) {
    // ngha, nghô, nghu — ngh + back = invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"ngh", L"a",      L"", Tone::None, kModern));
    EXPECT_FALSE(phon_.IsValidSyllable(L"ngh", L"\x00F4", L"", Tone::None, kModern));  // nghô
    EXPECT_FALSE(phon_.IsValidSyllable(L"ngh", L"u",      L"", Tone::None, kModern));
}

TEST_F(PhonotacticsIsValidSyllable, OnsetQuFreePassByDesign) {
    // qu agreement intentionally not enforced — see helper rationale comment.
    EXPECT_TRUE(phon_.IsValidSyllable(L"qu", L"a",      L"",  Tone::None, kModern));   // qua
    EXPECT_TRUE(phon_.IsValidSyllable(L"qu", L"a",      L"n", Tone::None, kModern));   // quan
    EXPECT_TRUE(phon_.IsValidSyllable(L"qu", L"\x00EA", L"",  Tone::None, kModern));   // quê
    EXPECT_TRUE(phon_.IsValidSyllable(L"qu", L"y",      L"",  Tone::None, kModern));   // quy
    EXPECT_TRUE(phon_.IsValidSyllable(L"qu", L"\x00E2", L"n", Tone::None, kModern));   // quân
    EXPECT_TRUE(phon_.IsValidSyllable(L"qu", L"\x0103", L"n", Tone::None, kModern));   // quăn
}

TEST_F(PhonotacticsIsValidSyllable, OnsetGiClusterUnaffected) {
    // "gi" is its own onset cluster (giáo, giải, giờ) — not subject to g/gh agreement.
    EXPECT_TRUE(phon_.IsValidSyllable(L"gi", L"a",      L"",  Tone::None, kModern));   // gia
    EXPECT_TRUE(phon_.IsValidSyllable(L"gi", L"ao",     L"",  Tone::None, kModern));   // giao
    EXPECT_TRUE(phon_.IsValidSyllable(L"gi", L"\x01A1", L"",  Tone::None, kModern));   // giờ-ish
}

TEST_F(PhonotacticsIsValidSyllable, OtherOnsetsNotSubjectToAgreement) {
    // b/d/h/l/m/n/p/r/s/t/v/x and ch/kh/nh/ph/th/tr accept any vowel.
    EXPECT_TRUE(phon_.IsValidSyllable(L"b",  L"e", L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"b",  L"a", L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"th", L"e", L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"th", L"a", L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"tr", L"a", L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"tr", L"i", L"",  Tone::None, kModern));
    EXPECT_TRUE(phon_.IsValidSyllable(L"",   L"a", L"n", Tone::None, kModern));   // vowel-initial
}

TEST_F(PhonotacticsIsValidSyllable, OnsetAgreementUsesFirstVowelOfDiphthong) {
    // Agreement is checked against the FIRST vowel of vowelSeq.
    EXPECT_TRUE (phon_.IsValidSyllable(L"ngh", L"i\x00EA", L"u", Tone::None, kModern));   // nghiêu (first 'i' front)
    EXPECT_FALSE(phon_.IsValidSyllable(L"ng",  L"i\x00EA", L"u", Tone::None, kModern));   // ngiêu invalid
    EXPECT_TRUE (phon_.IsValidSyllable(L"k",   L"i\x00EA", L"n", Tone::None, kModern));   // kiên
    EXPECT_FALSE(phon_.IsValidSyllable(L"c",   L"i\x00EA", L"n", Tone::None, kModern));   // ciên invalid
    EXPECT_FALSE(phon_.IsValidSyllable(L"k",   L"oa",      L"n", Tone::None, kModern));   // koan invalid (first 'o' back)
    EXPECT_FALSE(phon_.IsValidSyllable(L"gh",  L"oa",      L"",  Tone::None, kModern));   // gh + back invalid
    EXPECT_TRUE (phon_.IsValidSyllable(L"gh",  L"e",       L"o", Tone::None, kModern));   // gheo first 'e' front
}

//=============================================================================
// IsValidSyllable — VCPair vowel-coda compatibility (T2.1 Day-2).
// Per-nucleus allowed-coda bitmask sourced from VietnamesePhonologyData.h
// (canonical table, shared with the CharState path in PhonotacticsValidator).
// Replaces the coarser N1/N2/N3 partition that lived here in T3.
// Lenient fall-through for nuclei without a VCPair entry.
//=============================================================================

TEST_F(PhonotacticsIsValidSyllable, VCPairOpenNucleusAcceptsAnyCoda) {
    // a, e, oa carry F_ALL — every coda is allowed.
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"ng", Tone::None,  kModern));   // bang
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"nh", Tone::None,  kModern));   // banh
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"ch", Tone::Acute, kModern));   // bách
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"a", L"c",  Tone::Acute, kModern));   // bác
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"e", L"nh", Tone::None,  kModern));   // VCPair e: F_ALL
    EXPECT_TRUE(phon_.IsValidSyllable(L"b", L"e", L"ch", Tone::Acute, kModern));   // VCPair e: F_ALL
    EXPECT_TRUE(phon_.IsValidSyllable(L"h", L"oa", L"ng", Tone::None, kModern));   // hoang
}

TEST_F(PhonotacticsIsValidSyllable, VCPairRejectsCodaOutsideAllowedFinals) {
    // â/ă/o/ô/u/ư carry F_NO_CH_NH — nh and ch are rejected, others ok.
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"o",       L"nh", Tone::None,  kModern));  // VCPair o: F_NO_CH_NH
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"\x0103",  L"nh", Tone::None,  kModern));  // ă: F_NO_CH_NH
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"\x00F4",  L"ch", Tone::Acute, kModern));  // ô: F_NO_CH_NH
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"\x00E2",  L"nh", Tone::None,  kModern));  // â: F_NO_CH_NH
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"u",       L"ch", Tone::Acute, kModern));  // u: F_NO_CH_NH
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"u",       L"t",  Tone::Acute, kModern));  // bút (C3 is fine)
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"\x00F4",  L"ng", Tone::None,  kModern));  // bông
}

TEST_F(PhonotacticsIsValidSyllable, VCPairRestrictsCFrontVowels) {
    // VCPair: ê has no F_ng; i has no F_ng. ê/i still accept C3 + ch (C2).
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"\x00EA", L"ng", Tone::None,  kModern));  // bêng wrong
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"i",      L"ng", Tone::None,  kModern));  // bing wrong
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"\x00EA", L"nh", Tone::None,  kModern));  // bênh
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"i",      L"nh", Tone::None,  kModern));  // binh
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"i",      L"ch", Tone::Acute, kModern));  // bích
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"i",      L"c",  Tone::Acute, kModern));  // VCPair i allows c
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"\x00EA", L"c",  Tone::Acute, kModern));  // VCPair ê allows c
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"i",      L"m",  Tone::None,  kModern));  // bim
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"i",      L"t",  Tone::Acute, kModern));  // bít
}

TEST_F(PhonotacticsIsValidSyllable, VCPairTightlyConstrainsRareNuclei) {
    // VCPair: ơ allows only m/n/p/t; y allows only t; uy carries F_ch|F_n|F_nh|F_t.
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"\x01A1", L"n",  Tone::None, kModern));   // bơn
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"\x01A1", L"ng", Tone::None, kModern));   // ơ rejects ng
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"\x01A1", L"c",  Tone::Acute, kModern));  // ơ rejects c
    EXPECT_FALSE(phon_.IsValidSyllable(L"h", L"uy",     L"ng", Tone::None, kModern));   // uy rejects ng
    EXPECT_FALSE(phon_.IsValidSyllable(L"h", L"uy",     L"c",  Tone::Acute, kModern));  // uy rejects c
    EXPECT_TRUE (phon_.IsValidSyllable(L"h", L"uy",     L"nh", Tone::Grave, kModern));  // huỳnh
}

TEST_F(PhonotacticsIsValidSyllable, VCPairMultiVowelRules) {
    // iê: F_c|F_m|F_n|F_ng|F_p|F_t — accepts ng but rejects nh, ch.
    EXPECT_TRUE (phon_.IsValidSyllable(L"b", L"i\x00EA",  L"ng", Tone::None, kModern));   // biêng
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"i\x00EA",  L"nh", Tone::None, kModern));   // VCPair iê rejects nh
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"i\x00EA",  L"ch", Tone::Acute, kModern));  // VCPair iê rejects ch
    // uô / ươ: same shape (no ch, nh).
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"u\x00F4", L"nh", Tone::None,  kModern));   // buônh wrong
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"\x01B0\x01A1", L"ch", Tone::Acute, kModern));  // bươch wrong
    // oă: F_c|F_n|F_ng|F_t — accepts coda set, rejects nh/ch/m/p.
    EXPECT_FALSE(phon_.IsValidSyllable(L"",  L"o\x0103", L"nh", Tone::None, kModern));    // oănh wrong
    // uâ: F_n|F_ng|F_t.
    EXPECT_FALSE(phon_.IsValidSyllable(L"t", L"u\x00E2", L"nh", Tone::None, kModern));    // tuânh wrong
    // uê: F_ch|F_n|F_nh.
    EXPECT_TRUE (phon_.IsValidSyllable(L"th", L"u\x00EA", L"nh", Tone::None, kModern));   // thuênh ok per VCPair
}

TEST_F(PhonotacticsIsValidSyllable, VCPairUnknownNucleusFallsThroughLeniently) {
    // Nuclei that don't have a VCPair entry (e.g. orthographic loanword-friendly
    // sequences not in the canonical table) accept any coda.
    EXPECT_TRUE(phon_.IsValidSyllable(L"x", L"oo", L"ng", Tone::None, kModern));   // xoong (oo: F_c|F_ng → valid)
}

TEST_F(PhonotacticsIsValidSyllable, ClosedVowelRuleStillFiresBeforeVCPair) {
    // Closed-vowel rule rejects any coda regardless of VCPair lookup; closed
    // check runs first.
    EXPECT_FALSE(phon_.IsValidSyllable(L"b", L"ai", L"n", Tone::None, kModern));   // ai is closed → invalid
}

//=============================================================================
// CanComplete — partial syllable extensibility (auto-exclusion gate)
//=============================================================================

class PhonotacticsCanComplete : public ::testing::Test {
protected:
    Phonotactics phon_;
};

TEST_F(PhonotacticsCanComplete, EmptyIsCompletable) {
    // Empty string is trivially extensible into any syllable.
    EXPECT_TRUE(phon_.CanComplete(L""));
}

TEST_F(PhonotacticsCanComplete, ValidSyllableIsCompletable) {
    // Already-valid syllables are by definition completable.
    EXPECT_TRUE(phon_.CanComplete(L"ba"));
    EXPECT_TRUE(phon_.CanComplete(L"ban"));
    EXPECT_TRUE(phon_.CanComplete(L"hoa"));
}

TEST_F(PhonotacticsCanComplete, ValidPrefixIsCompletable) {
    // Bare consonant — can be extended with vowels.
    EXPECT_TRUE(phon_.CanComplete(L"b"));
    EXPECT_TRUE(phon_.CanComplete(L"th"));
    EXPECT_TRUE(phon_.CanComplete(L"ng"));
}

TEST_F(PhonotacticsCanComplete, ClosedVowelPlusVowelRejected) {
    // "gach" is fully closed (a is N3 + ch coda). Adding 'a' (→"gacha") cannot
    // form a valid Vietnamese syllable — auto-exclusion case.
    EXPECT_FALSE(phon_.CanComplete(L"gacha"));
    // Sibling case from same bug: "gachw" cannot extend.
    EXPECT_FALSE(phon_.CanComplete(L"gachw"));
}

TEST_F(PhonotacticsCanComplete, NonsenseClusterRejected) {
    // "bcd" — no vowel, can't form syllable.
    EXPECT_FALSE(phon_.CanComplete(L"bcd"));
}

//=============================================================================
// TypingEngine DI plumbing — verifies G-2.1 wiring:
// TypingEngine accepts a custom IPhonotactics via ctor, default-binds to
// Phonotactics::Default() singleton, behavior unchanged from G-1 baseline.
//=============================================================================

TEST(TypingEngineDI, AcceptsCustomPhonotactics) {
    Phonotactics customPhonotactics;
    TypingConfig config;
    TypingEngine engine(config, customPhonotactics);
    // Smoke: engine constructible + functional through DI ctor.
    engine.PushChar(L'a');
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST(TypingEngineDI, SingleArgCtorBindsDefaultPhonotactics) {
    // Existing single-arg ctor must still compile and behave identically;
    // it delegates to Phonotactics::Default() internally.
    TypingConfig config;
    TypingEngine engine(config);
    engine.PushChar(L'a');
    EXPECT_EQ(engine.Peek(), L"a");
}

TEST(PhonotacticsDefault, ReturnsStableSingleton) {
    // Default() must return the same instance every call (singleton lifetime
    // covers any TypingEngine that bound to it).
    const Phonotactics& a = Phonotactics::Default();
    const Phonotactics& b = Phonotactics::Default();
    EXPECT_EQ(&a, &b);
}

//=============================================================================
// IPhonologyRules DI plumbing — verifies T2.1 D3:
// Phonotactics accepts a custom IPhonologyRules pack, rule queries flow through
// the contract, default ctor binds to DefaultPhonologyRules.
//=============================================================================

TEST(DefaultPhonologyRules, IsSingleton) {
    const DefaultPhonologyRules& a = DefaultPhonologyRules::Default();
    const DefaultPhonologyRules& b = DefaultPhonologyRules::Default();
    EXPECT_EQ(&a, &b);
}

TEST(DefaultPhonologyRules, MatchesFreeFunctionContracts) {
    // Default impl should produce the same answers as the free functions in
    // VietnamesePhonologyData.h (it just forwards).
    const DefaultPhonologyRules& rules = DefaultPhonologyRules::Default();
    EXPECT_TRUE (rules.IsFrontBaseVowel(L'e'));
    EXPECT_TRUE (rules.IsFrontBaseVowel(L'i'));
    EXPECT_TRUE (rules.IsFrontBaseVowel(L'y'));
    EXPECT_FALSE(rules.IsFrontBaseVowel(L'a'));
    EXPECT_FALSE(rules.IsFrontBaseVowel(L'o'));
    EXPECT_FALSE(rules.IsFrontBaseVowel(L'u'));
    // Sample VCPair lookup: a → F_ALL (all 9 finals).
    uint32_t aKey = Key1(VowelSlot(kA, kNone));
    EXPECT_EQ(rules.AllowedFinalsForVowelKey(aKey), F_ALL);
    // ơ → F_m | F_n | F_p | F_t only.
    uint32_t oHornKey = Key1(VowelSlot(kO, kHorn));
    EXPECT_EQ(rules.AllowedFinalsForVowelKey(oHornKey), F_m | F_n | F_p | F_t);
}

namespace {
// Stub flipping the front/back classification (a becomes front, i becomes back).
// Used to prove the injected pack — not the default singleton — is consulted.
class FlippedFrontVowelRules final : public IPhonologyRules {
public:
    [[nodiscard]] bool IsFrontBaseVowel(wchar_t base) const noexcept override {
        return !DefaultPhonologyRules::Default().IsFrontBaseVowel(base);
    }
    [[nodiscard]] uint16_t AllowedFinalsForVowelKey(uint32_t key) const noexcept override {
        return DefaultPhonologyRules::Default().AllowedFinalsForVowelKey(key);
    }
};

// Stub returning no VCPair entry for any nucleus (forces lenient pass-through
// on every IsCodaValidForNucleus call).
class NoCodaRestrictionRules final : public IPhonologyRules {
public:
    [[nodiscard]] bool IsFrontBaseVowel(wchar_t base) const noexcept override {
        return DefaultPhonologyRules::Default().IsFrontBaseVowel(base);
    }
    [[nodiscard]] uint16_t AllowedFinalsForVowelKey(uint32_t /*key*/) const noexcept override {
        return 0;
    }
};
}  // namespace

TEST(PhonotacticsDI, CustomRulesFlipOnsetAgreement) {
    FlippedFrontVowelRules flipped;
    Phonotactics phon(flipped);
    // Default: ke is valid (k wants front, e is front). Flipped: e is back → invalid.
    EXPECT_FALSE(phon.IsValidSyllable(L"k", L"e", L"", Tone::None, true));
    // Default: ka is invalid (k wants front, a is back). Flipped: a is front → valid.
    EXPECT_TRUE (phon.IsValidSyllable(L"k", L"a", L"", Tone::None, true));
}

TEST(PhonotacticsDI, CustomRulesDropsVCPairRestrictions) {
    NoCodaRestrictionRules lenient;
    Phonotactics phon(lenient);
    // Default: ơng invalid (ơ allows only m/n/p/t). Custom: returns 0 → lenient → valid.
    EXPECT_TRUE(phon.IsValidSyllable(L"b", L"\x01A1", L"ng", Tone::None, true));
    // Default: yng invalid (y allows only t). Custom: lenient → valid.
    EXPECT_TRUE(phon.IsValidSyllable(L"",  L"y",      L"ng", Tone::None, true));
}

TEST(PhonotacticsDI, DefaultCtorBindsDefaultRules) {
    // Default-constructed Phonotactics enforces canonical Vietnamese rules.
    Phonotactics phon;
    EXPECT_FALSE(phon.IsValidSyllable(L"k", L"a",      L"",   Tone::None, true));   // k requires front
    EXPECT_FALSE(phon.IsValidSyllable(L"c", L"i",      L"",   Tone::None, true));   // c requires back
    EXPECT_FALSE(phon.IsValidSyllable(L"b", L"\x01A1", L"ng", Tone::None, true));   // ơ + ng forbidden
    EXPECT_TRUE (phon.IsValidSyllable(L"k", L"e",      L"",   Tone::None, true));   // k + e ok
    EXPECT_TRUE (phon.IsValidSyllable(L"b", L"a",      L"ng", Tone::None, true));   // a + ng ok
}

//=============================================================================
// PhonologyRulePackId factory (T2.1 D4 — sprint close).
// One factory hook for callers; future RulePackId values plug in via the
// switch in PhonologyRulePackFactory.h without touching consumers.
//=============================================================================

TEST(PhonologyRulePackFactory, DefaultIdReturnsDefaultRules) {
    const IPhonologyRules& fromFactory = GetRulePack(PhonologyRulePackId::Default);
    const IPhonologyRules& direct      = DefaultPhonologyRules::Default();
    EXPECT_EQ(&fromFactory, &direct);
}

TEST(PhonologyRulePackFactory, ReturnIsStableSingleton) {
    const IPhonologyRules& a = GetRulePack(PhonologyRulePackId::Default);
    const IPhonologyRules& b = GetRulePack(PhonologyRulePackId::Default);
    EXPECT_EQ(&a, &b);
}

TEST(PhonologyRulePackFactory, FactoryUsableAsDIDependency) {
    // Caller pattern: pull rule pack via factory, hand to Phonotactics ctor.
    // Mirrors how IOutputInjector consumers construct from the factory result.
    Phonotactics phon(GetRulePack(PhonologyRulePackId::Default));
    EXPECT_TRUE (phon.IsValidSyllable(L"b", L"a", L"ng", Tone::None, true));   // a + ng ok
    EXPECT_FALSE(phon.IsValidSyllable(L"k", L"a", L"",   Tone::None, true));   // k requires front
}

}  // namespace
}  // namespace Phonology
}  // namespace NextKey
