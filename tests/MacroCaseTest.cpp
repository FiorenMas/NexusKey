// VKey - Macro expansion decision unit tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include "core/MacroCase.h"

namespace NextKey::Macro {
namespace {

struct AsciiCaseMapper final : CaseMapper {
    void Upper(wchar_t* buf, std::size_t n) const override {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] >= L'a' && buf[i] <= L'z') buf[i] = buf[i] - L'a' + L'A';
    }
    void Lower(wchar_t* buf, std::size_t n) const override {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] >= L'A' && buf[i] <= L'Z') buf[i] = buf[i] - L'A' + L'a';
    }
};

// PlanFixture helper — shared by all Plan test suites (Tasks 4b–4e)
struct PlanFixture {
    AsciiCaseMapper mapper;
    std::wstring raw;
    std::wstring prevComp;
    std::vector<uint8_t> widths;
    std::unordered_map<std::wstring, std::wstring> table;
    bool crossCommit = false;
    CodeTable codeTable = CodeTable::Unicode;
    bool autoCaps = false;
    wchar_t trigger = L' ';
    std::size_t threshold = 200;

    [[nodiscard]] MacroPlan Run() const {
        PlanInputs in{
            .rawMacroBuffer        = raw,
            .previousComposition   = prevComp,
            .previousEncodedWidths = widths,
            .macroTable            = table,
            .macroCrossCommit      = crossCommit,
            .currentCodeTable      = codeTable,
            .autoCapsEnabled       = autoCaps,
            .triggerChar           = trigger,
            .clipboardThreshold    = threshold,
        };
        return Plan(in, mapper);
    }
};

// -------------------------------------------------------------------------
// Match priorities + stored-key case rule
// -------------------------------------------------------------------------

TEST(PlanMatchPrioritiesTest, FullBufferExact) {
    PlanFixture f;
    f.table[L"BTW"] = L"by the way";
    f.raw = L"BTW";
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"by the way");
    EXPECT_FALSE(p.isPartOfMacro);   // trigger == L' ' is not "> L' '"
}

TEST(PlanMatchPrioritiesTest, FullBufferLowerFallback) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"BTW";
    f.trigger = L'.';
    auto p = f.Run();
    // No exact match for "BTW.", lowered "btw." also misses.
    // Priority 2: try "BTW" exact (miss), then "btw" lower → hit.
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"by the way");
}

TEST(PlanMatchPrioritiesTest, BufferWithoutTriggerExact) {
    PlanFixture f;
    f.table[L"BTW"] = L"by the way";
    f.raw = L"BTW.";
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_FALSE(p.isPartOfMacro);    // P2 does NOT set isPartOfMacro — trigger '.' passes through to the document
}

TEST(PlanMatchPrioritiesTest, BufferWithoutTriggerLower) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"btw.";
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_FALSE(p.isPartOfMacro);    // P2 does NOT set isPartOfMacro — trigger '.' passes through to the document
}

TEST(PlanMatchPrioritiesTest, PrevCompositionWithTrigger) {
    PlanFixture f;
    f.table[L"chao."] = L"xin chao";
    f.raw = L".";
    f.prevComp = L"CHAO";
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_TRUE(p.isPartOfMacro);
}

TEST(PlanMatchPrioritiesTest, PrevCompositionWithoutTrigger) {
    PlanFixture f;
    f.table[L"chao"] = L"xin chao";
    f.raw = L"";
    f.prevComp = L"CHAO";
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_FALSE(p.isPartOfMacro);
}

TEST(PlanMatchPrioritiesTest, NoMatchReturnsFalse) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"xyz";
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_FALSE(p.matched);
}

TEST(PlanStoredKeyCaseRuleTest, UppercaseKeyRejectsLowercaseTyping) {
    // Uppercase-key contract: only exact case matches.
    PlanFixture f;
    f.table[L"BTW"] = L"BY THE WAY";
    f.raw = L"btw";
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_FALSE(p.matched);
}

TEST(PlanStoredKeyCaseRuleTest, LowercaseKeyAcceptsAnyCase) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    for (auto* probe : {L"btw", L"BTW", L"Btw", L"bTw"}) {
        f.raw = probe;
        f.trigger = L' ';
        auto p = f.Run();
        // probe is always ASCII ("btw" variants) — explicit narrow avoids
        // MSVC /WX C4244 from std::string(wchar_t*, wchar_t*).
        const char narrow[4] = {static_cast<char>(probe[0]),
                                static_cast<char>(probe[1]),
                                static_cast<char>(probe[2]),
                                0};
        EXPECT_TRUE(p.matched) << "probe = " << narrow;
    }
}

// -------------------------------------------------------------------------
// Backspace count — covers all five branches of the bs-count tree
// -------------------------------------------------------------------------

TEST(PlanBsCountTest, MatchedViaCompositionUnicode) {
    PlanFixture f;
    f.table[L"chao"] = L"xin chao";
    f.prevComp = L"chao";
    f.raw = L"";
    f.trigger = L' ';
    f.codeTable = CodeTable::Unicode;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 4u);
}

TEST(PlanBsCountTest, MatchedViaCompositionTcvn3Widths) {
    PlanFixture f;
    f.table[L"chao"] = L"xin chao";
    f.prevComp = L"chao";   // Unicode 4 chars
    f.raw = L"";
    f.trigger = L' ';
    f.codeTable = CodeTable::TCVN3;
    f.widths = {1, 1, 2, 1};   // 5 bytes total under encoding
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 5u);
}

TEST(PlanBsCountTest, CrossCommitWithTrigger) {
    PlanFixture f;
    f.table[L"a.i"] = L"artificial intelligence";
    f.raw = L"a.i.";        // includes trigger
    f.crossCommit = true;
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 3u);   // 4 - 1 (trigger)
}

TEST(PlanBsCountTest, CrossCommitWithoutTrigger) {
    PlanFixture f;
    f.table[L"a.i"] = L"artificial intelligence";
    f.raw = L"a.i";
    f.crossCommit = true;
    f.trigger = L' ';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 3u);   // trigger == ' ', no decrement
}

TEST(PlanBsCountTest, DefaultBranchWithTrigger) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"btw.";
    f.trigger = L'.';
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 3u);   // 4 - 1 (trigger), default branch
}

TEST(PlanBsCountTest, RawMatchWithNonEmptyPrevCompUnicode) {
    // Raw-buffer match (P1) with prevComp non-empty + non-crossCommit:
    // bsCount comes from previousComposition.size(), not rawMacroBuffer.size().
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"btw";
    f.prevComp = L"hoa";       // 3 Unicode chars displayed on screen
    f.crossCommit = false;
    f.trigger = L' ';
    f.codeTable = CodeTable::Unicode;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 3u);   // previousComposition.size()
}

TEST(PlanBsCountTest, RawMatchWithNonEmptyPrevCompTcvn3Widths) {
    // Same branch, non-Unicode encoding: bsCount sums previousEncodedWidths.
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"btw";
    f.prevComp = L"hoa";
    f.crossCommit = false;
    f.trigger = L' ';
    f.codeTable = CodeTable::TCVN3;
    f.widths = {1, 2, 1};   // 4 bytes total under encoding
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.bsCount, 4u);
}

// -------------------------------------------------------------------------
// Auto-caps — decision gates and \n-escape preservation
// -------------------------------------------------------------------------

TEST(PlanAutoCapsDecisionTest, AutoCapsDisabledNoTransform) {
    PlanFixture f;
    f.table[L"btw"] = L"by the way";
    f.raw = L"BTW";
    f.trigger = L'.';
    f.autoCaps = false;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"by the way");   // unchanged
}

TEST(PlanAutoCapsDecisionTest, MatchedExactSuppressesAutoCaps) {
    PlanFixture f;
    f.table[L"BTW"] = L"by the way";   // uppercase key (exact-match contract)
    f.raw = L"BTW";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"by the way");   // matchedExact → no transform
}

TEST(PlanAutoCapsDecisionTest, MatchedViaCompositionSuppressesAutoCaps) {
    PlanFixture f;
    f.table[L"chao"] = L"xin chao";
    f.prevComp = L"CHAO";
    f.raw = L"";
    f.trigger = L' ';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"xin chao");   // composition match → no transform
}

TEST(PlanAutoCapsDecisionTest, ExpansionHasUppercaseSuppressesTransform) {
    PlanFixture f;
    f.table[L"omw"] = L"On My Way";   // expansion not all-lower
    f.raw = L"OMW";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"On My Way");
}

TEST(PlanAutoCapsDecisionTest, AllUpperRawTransformsToAllUpperExpansion) {
    PlanFixture f;
    f.table[L"omw"] = L"on my way";
    f.raw = L"OMW";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"ON MY WAY");
}

TEST(PlanAutoCapsDecisionTest, FirstUpperRawTransformsToFirstUpperExpansion) {
    PlanFixture f;
    f.table[L"omw"] = L"on my way";
    f.raw = L"Omw";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"On My Way");
}

TEST(PlanAutoCapsDecisionTest, FirstUpperRawTransformsToTitleCaseVietnamese) {
    PlanFixture f;
    f.table[L"lhq"] = L"liên hiệp quốc";
    f.raw = L"Lhq";
    f.trigger = L' ';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"Liên Hiệp Quốc");
}

TEST(PlanAutoCapsEscapeTest, AllUpperSkipsBackslashN) {
    PlanFixture f;
    f.table[L"sig"] = L"name\\nemail";   // \n escape inside expansion
    f.raw = L"SIG";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    // 'n' in '\n' must remain lowercase; everything else uppercase.
    EXPECT_EQ(p.expansion, L"NAME\\nEMAIL");
}

TEST(PlanAutoCapsEscapeTest, AllUpperPlainNStillUppercases) {
    PlanFixture f;
    f.table[L"hi"] = L"hello name";   // plain 'n' (no leading backslash)
    f.raw = L"HI";
    f.trigger = L'.';
    f.autoCaps = true;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_EQ(p.expansion, L"HELLO NAME");
}

// -------------------------------------------------------------------------
// useClipboard threshold
// -------------------------------------------------------------------------

TEST(PlanUseClipboardTest, UnicodeAboveThresholdSetsClipboardTrue) {
    PlanFixture f;
    std::wstring big(250, L'x');   // > threshold (200)
    f.table[L"big"] = big;
    f.raw = L"big";
    f.trigger = L' ';
    f.codeTable = CodeTable::Unicode;
    f.threshold = 200;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_TRUE(p.useClipboard);
}

TEST(PlanUseClipboardTest, UnicodeAtOrBelowThresholdKeepsSendInput) {
    PlanFixture f;
    std::wstring small(200, L'x');   // == threshold; uses ">" not ">="
    f.table[L"small"] = small;
    f.raw = L"small";
    f.trigger = L' ';
    f.codeTable = CodeTable::Unicode;
    f.threshold = 200;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_FALSE(p.useClipboard);
}

TEST(PlanUseClipboardTest, NonUnicodeAlwaysSendInputRegardlessOfSize) {
    PlanFixture f;
    std::wstring big(500, L'x');
    f.table[L"big"] = big;
    f.raw = L"big";
    f.trigger = L' ';
    f.codeTable = CodeTable::TCVN3;
    f.threshold = 200;
    auto p = f.Run();
    EXPECT_TRUE(p.matched);
    EXPECT_FALSE(p.useClipboard);   // non-Unicode → always SendInput
}

// -------------------------------------------------------------------------
// Clipboard escape expansion + segment builder
// -------------------------------------------------------------------------

TEST(ClipboardEscapesTest, EmptyInput) {
    EXPECT_EQ(ExpandEscapesForClipboard(L""), L"");
}

TEST(ClipboardEscapesTest, NoEscapes) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"hello world"), L"hello world");
}

TEST(ClipboardEscapesTest, SingleNewlineEscape) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"line1\\nline2"), L"line1\r\nline2");
}

TEST(ClipboardEscapesTest, MultipleNewlinesEscape) {
    EXPECT_EQ(ExpandEscapesForClipboard(L"a\\nb\\nc"), L"a\r\nb\r\nc");
}

TEST(ClipboardEscapesTest, LiteralBackslashFollowedByNonN) {
    // \\t is NOT an escape — only \n is recognized. \t passes through verbatim.
    EXPECT_EQ(ExpandEscapesForClipboard(L"a\\tb"), L"a\\tb");
}

TEST(BuildSegmentsTest, EmptyExpansion) {
    auto segs = BuildSegments(L"", CodeTable::Unicode);
    EXPECT_TRUE(segs.empty());
}

TEST(BuildSegmentsTest, PureTextNoNewline) {
    auto segs = BuildSegments(L"hello", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    EXPECT_EQ(segs[0].text, L"hello");
}

TEST(BuildSegmentsTest, TextNewlineText) {
    auto segs = BuildSegments(L"line1\\nline2", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_FALSE(segs[0].isReturn); EXPECT_EQ(segs[0].text, L"line1");
    EXPECT_TRUE(segs[1].isReturn);  EXPECT_EQ(segs[1].text, L"");
    EXPECT_FALSE(segs[2].isReturn); EXPECT_EQ(segs[2].text, L"line2");
}

TEST(BuildSegmentsTest, LeadingNewline) {
    auto segs = BuildSegments(L"\\nhello", CodeTable::Unicode);
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_TRUE(segs[0].isReturn);
    EXPECT_FALSE(segs[1].isReturn); EXPECT_EQ(segs[1].text, L"hello");
}

TEST(BuildSegmentsTest, NonUnicodeSingleByteEncoding) {
    // 'à' (U+00E0) under TCVN3 maps to 0xB5 (single byte).
    // Verify ConvertChar is invoked and result has the encoded byte, not 'à'.
    auto segs = BuildSegments(L"à", CodeTable::TCVN3);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    ASSERT_EQ(segs[0].text.size(), 1u);
    EXPECT_NE(segs[0].text[0], L'à');               // must have been converted
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[0]), 0x00B5);
}

TEST(BuildSegmentsTest, NonUnicodeMultiUnitEncoding) {
    // 'á' (U+00E1) under VNIWindows maps to encoded value 0xF961 → 2 units:
    // units[0] = LOBYTE = 0x61 ('a'), units[1] = HIBYTE = 0xF9 (tone marker).
    // Both units must be appended to the segment text in order.
    auto segs = BuildSegments(L"á", CodeTable::VNIWindows);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FALSE(segs[0].isReturn);
    ASSERT_EQ(segs[0].text.size(), 2u);
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[0]), 0x0061);
    EXPECT_EQ(static_cast<uint16_t>(segs[0].text[1]), 0x00F9);
}

}  // namespace
}  // namespace NextKey::Macro
