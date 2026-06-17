// VKey - CodeTableConverter Unit Tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include "core/engine/CodeTableConverter.h"

namespace NextKey {
namespace CodeTableConverterTests {
namespace {

using CT = CodeTable;

// ============================================================================
// UNICODE PASSTHROUGH — all chars returned as-is
// ============================================================================

TEST(CodeTableConverter, Unicode_Passthrough) {
    auto r = CodeTableConverter::ConvertChar(L'a', CT::Unicode);
    EXPECT_EQ(r.units[0], L'a');
    EXPECT_EQ(r.count, 1);
}

TEST(CodeTableConverter, Unicode_VietnameseChar) {
    // á (U+00E1) should pass through unchanged in Unicode mode
    auto r = CodeTableConverter::ConvertChar(0x00E1, CT::Unicode);
    EXPECT_EQ(r.units[0], 0x00E1);
    EXPECT_EQ(r.count, 1);
}

// ============================================================================
// ASCII PASSTHROUGH — non-Vietnamese chars pass through in all tables
// ============================================================================

TEST(CodeTableConverter, ASCII_Passthrough_AllTables) {
    wchar_t asciiChars[] = {L'a', L' ', L'1', L'b', L'z'};
    CT tables[] = {CT::TCVN3, CT::VNIWindows, CT::UnicodeCompound, CT::VietnameseLocale};

    for (auto table : tables) {
        for (auto ch : asciiChars) {
            auto r = CodeTableConverter::ConvertChar(ch, table);
            EXPECT_EQ(r.units[0], ch) << "char=" << static_cast<int>(ch) << " table=" << static_cast<int>(table);
            EXPECT_EQ(r.count, 1);
        }
    }
}

// ============================================================================
// TCVN3 — always 1 byte output
// ============================================================================

TEST(CodeTableConverter, TCVN3_â) {
    // â (U+00E2) → 0xA9
    auto r = CodeTableConverter::ConvertChar(0x00E2, CT::TCVN3);
    EXPECT_EQ(r.units[0], 0xA9);
    EXPECT_EQ(r.count, 1);
}

TEST(CodeTableConverter, TCVN3_Â) {
    // Â (U+00C2) → 0xA2
    auto r = CodeTableConverter::ConvertChar(0x00C2, CT::TCVN3);
    EXPECT_EQ(r.units[0], 0xA2);
    EXPECT_EQ(r.count, 1);
}

TEST(CodeTableConverter, TCVN3_á) {
    // á (U+00E1) → 0xB8
    auto r = CodeTableConverter::ConvertChar(0x00E1, CT::TCVN3);
    EXPECT_EQ(r.units[0], 0xB8);
    EXPECT_EQ(r.count, 1);
}

TEST(CodeTableConverter, TCVN3_đ) {
    // đ (U+0111) → 0xAE
    auto r = CodeTableConverter::ConvertChar(0x0111, CT::TCVN3);
    EXPECT_EQ(r.units[0], 0xAE);
    EXPECT_EQ(r.count, 1);
}

TEST(CodeTableConverter, TCVN3_ấ) {
    // ấ (U+1EA5) → 0xCA
    auto r = CodeTableConverter::ConvertChar(0x1EA5, CT::TCVN3);
    EXPECT_EQ(r.units[0], 0xCA);
    EXPECT_EQ(r.count, 1);
}

TEST(CodeTableConverter, TCVN3_ệ) {
    // ệ (U+1EC7) → 0xD6
    auto r = CodeTableConverter::ConvertChar(0x1EC7, CT::TCVN3);
    EXPECT_EQ(r.units[0], 0xD6);
    EXPECT_EQ(r.count, 1);
}

TEST(CodeTableConverter, TCVN3_AlwaysSingleUnit) {
    // Verify all TCVN3 conversions produce count=1
    wchar_t vietChars[] = {0x00E1, 0x00E0, 0x1EA3, 0x00E3, 0x1EA1,  // á à ả ã ạ
                           0x00E2, 0x0103, 0x0111, 0x00EA, 0x00F4,   // â ă đ ê ô
                           0x01A1, 0x01B0, 0x00ED, 0x00FD};          // ơ ư í ý
    for (auto ch : vietChars) {
        auto r = CodeTableConverter::ConvertChar(ch, CT::TCVN3);
        EXPECT_EQ(r.count, 1) << "char U+" << std::hex << static_cast<int>(ch);
    }
}

// ============================================================================
// VNI Windows — 1 or 2 units
// ============================================================================

TEST(CodeTableConverter, VNI_á_TwoUnits) {
    // á (U+00E1) → {0x61, 0xF9} (base 'a' + tone mark)
    auto r = CodeTableConverter::ConvertChar(0x00E1, CT::VNIWindows);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.units[0], 0x61);  // LOBYTE = 'a'
    EXPECT_EQ(r.units[1], 0xF9);  // HIBYTE = tone
}

TEST(CodeTableConverter, VNI_đ_SingleUnit) {
    // đ (U+0111) → 0xF1 (single)
    auto r = CodeTableConverter::ConvertChar(0x0111, CT::VNIWindows);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.units[0], 0xF1);
}

TEST(CodeTableConverter, VNI_Đ_SingleUnit) {
    // Đ (U+0110) → 0xD1 (single)
    auto r = CodeTableConverter::ConvertChar(0x0110, CT::VNIWindows);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.units[0], 0xD1);
}

TEST(CodeTableConverter, VNI_â_TwoUnits) {
    // â (U+00E2) → {0x61, 0xE2} (base 'a' + circumflex mark)
    auto r = CodeTableConverter::ConvertChar(0x00E2, CT::VNIWindows);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.units[0], 0x61);  // LOBYTE
    EXPECT_EQ(r.units[1], 0xE2);  // HIBYTE
}

TEST(CodeTableConverter, VNI_ơ_SingleUnit) {
    // ơ (U+01A1) → 0xF4 (repurposed Ô)
    auto r = CodeTableConverter::ConvertChar(0x01A1, CT::VNIWindows);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.units[0], 0xF4);
}

// ============================================================================
// Unicode Compound — base + combining mark
// ============================================================================

TEST(CodeTableConverter, Compound_á_TwoUnits) {
    // á (U+00E1) → {U+0061, U+0301} (base 'a' + combining acute)
    auto r = CodeTableConverter::ConvertChar(0x00E1, CT::UnicodeCompound);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.units[0], 0x0061);  // 'a'
    EXPECT_EQ(r.units[1], 0x0301);  // combining acute accent (sắc)
}

TEST(CodeTableConverter, Compound_à_TwoUnits) {
    // à (U+00E0) → {U+0061, U+0300} (base 'a' + combining grave)
    auto r = CodeTableConverter::ConvertChar(0x00E0, CT::UnicodeCompound);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.units[0], 0x0061);  // 'a'
    EXPECT_EQ(r.units[1], 0x0300);  // combining grave accent (huyền)
}

TEST(CodeTableConverter, Compound_ả_TwoUnits) {
    // ả (U+1EA3) → {U+0061, U+0309} (base 'a' + combining hook above)
    auto r = CodeTableConverter::ConvertChar(0x1EA3, CT::UnicodeCompound);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.units[0], 0x0061);  // 'a'
    EXPECT_EQ(r.units[1], 0x0309);  // combining hook above (hỏi)
}

TEST(CodeTableConverter, Compound_â_SingleUnit) {
    // â (U+00E2) → 0x00E2 (precomposed, no tone → single unit)
    auto r = CodeTableConverter::ConvertChar(0x00E2, CT::UnicodeCompound);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.units[0], 0x00E2);
}

TEST(CodeTableConverter, Compound_đ_SingleUnit) {
    // đ (U+0111) → 0x0111 (single, no combining mark)
    auto r = CodeTableConverter::ConvertChar(0x0111, CT::UnicodeCompound);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.units[0], 0x0111);
}

TEST(CodeTableConverter, Compound_ấ_TwoUnits) {
    // ấ (U+1EA5) → {U+00E2, U+0301} (â + combining acute)
    auto r = CodeTableConverter::ConvertChar(0x1EA5, CT::UnicodeCompound);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.units[0], 0x00E2);  // â
    EXPECT_EQ(r.units[1], 0x0301);  // combining acute
}

// ============================================================================
// CP 1258 (Vietnamese Locale) — 1 or 2 units
// ============================================================================

TEST(CodeTableConverter, CP1258_á_TwoUnits) {
    // á (U+00E1) → {0x61, 0xEC}
    auto r = CodeTableConverter::ConvertChar(0x00E1, CT::VietnameseLocale);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.units[0], 0x61);  // LOBYTE
    EXPECT_EQ(r.units[1], 0xEC);  // HIBYTE
}

TEST(CodeTableConverter, CP1258_đ_SingleUnit) {
    // đ (U+0111) → 0xF0
    auto r = CodeTableConverter::ConvertChar(0x0111, CT::VietnameseLocale);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.units[0], 0xF0);
}

TEST(CodeTableConverter, CP1258_â_SingleUnit) {
    // â (U+00E2) → 0x00E2 (same as Unicode precomposed)
    auto r = CodeTableConverter::ConvertChar(0x00E2, CT::VietnameseLocale);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.units[0], 0x00E2);
}

TEST(CodeTableConverter, CP1258_ư_SingleUnit) {
    // ư (U+01B0) → 0xFD
    auto r = CodeTableConverter::ConvertChar(0x01B0, CT::VietnameseLocale);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.units[0], 0xFD);
}

// ============================================================================
// Full word tests — "Việt" in each code table
// ============================================================================

TEST(CodeTableConverter, FullWord_Viet_TCVN3) {
    // V = passthrough, i = passthrough, ệ = 0xD6, t = passthrough
    std::wstring input = L"Vi\x1EC7t";  // Việt
    std::wstring result;
    for (wchar_t ch : input) {
        auto enc = CodeTableConverter::ConvertChar(ch, CT::TCVN3);
        result += enc.units[0];
        if (enc.count == 2) result += enc.units[1];
    }
    EXPECT_EQ(result.size(), 4u);  // TCVN3: all single units
    EXPECT_EQ(result[0], L'V');
    EXPECT_EQ(result[1], L'i');
    EXPECT_EQ(result[2], static_cast<wchar_t>(0xD6));
    EXPECT_EQ(result[3], L't');
}

TEST(CodeTableConverter, FullWord_Viet_VNI) {
    // V = pass, i = pass, ệ (U+1EC7) = {0x45, 0xE4} (2 units), t = pass
    std::wstring input = L"Vi\x1EC7t";
    int totalUnits = 0;
    for (wchar_t ch : input) {
        auto enc = CodeTableConverter::ConvertChar(ch, CT::VNIWindows);
        totalUnits += enc.count;
    }
    EXPECT_EQ(totalUnits, 5);  // V(1) + i(1) + ệ(2) + t(1)
}

TEST(CodeTableConverter, FullWord_Viet_Compound) {
    // V = pass, i = pass, ệ (U+1EC7) = {0x00EA, 0x0323} (2 units), t = pass
    std::wstring input = L"Vi\x1EC7t";
    auto enc = CodeTableConverter::ConvertChar(0x1EC7, CT::UnicodeCompound);
    EXPECT_EQ(enc.count, 2);
    EXPECT_EQ(enc.units[0], 0x00EA);  // ê (precomposed base)
    EXPECT_EQ(enc.units[1], 0x0323);  // combining dot below (nặng)
}

TEST(CodeTableConverter, FullWord_Viet_CP1258) {
    // ệ (U+1EC7) in CP1258: {0xEA, 0xF2}
    auto enc = CodeTableConverter::ConvertChar(0x1EC7, CT::VietnameseLocale);
    EXPECT_EQ(enc.count, 2);
    EXPECT_EQ(enc.units[0], 0xEA);  // LOBYTE
    EXPECT_EQ(enc.units[1], 0xF2);  // HIBYTE
}

// ============================================================================
// Width count verification — varies by table
// ============================================================================

TEST(CodeTableConverter, WidthCounts_MixedWord) {
    // Test "đẹp" — đ + ẹ + p
    // đ (U+0111): TCVN3=1, VNI=1, Compound=1, CP1258=1
    // ẹ (U+1EB9): TCVN3=1, VNI=2, Compound=2, CP1258=2
    // p: always 1
    auto tcvn3_d = CodeTableConverter::ConvertChar(0x0111, CT::TCVN3);
    auto tcvn3_e = CodeTableConverter::ConvertChar(0x1EB9, CT::TCVN3);
    EXPECT_EQ(tcvn3_d.count + tcvn3_e.count + 1, 3u);  // 1+1+1

    auto vni_d = CodeTableConverter::ConvertChar(0x0111, CT::VNIWindows);
    auto vni_e = CodeTableConverter::ConvertChar(0x1EB9, CT::VNIWindows);
    EXPECT_EQ(vni_d.count + vni_e.count + 1, 4u);  // 1+2+1

    auto comp_d = CodeTableConverter::ConvertChar(0x0111, CT::UnicodeCompound);
    auto comp_e = CodeTableConverter::ConvertChar(0x1EB9, CT::UnicodeCompound);
    EXPECT_EQ(comp_d.count + comp_e.count + 1, 4u);  // 1+2+1

    auto cp_d = CodeTableConverter::ConvertChar(0x0111, CT::VietnameseLocale);
    auto cp_e = CodeTableConverter::ConvertChar(0x1EB9, CT::VietnameseLocale);
    EXPECT_EQ(cp_d.count + cp_e.count + 1, 4u);  // 1+2+1
}

// ============================================================================
// STRING-LEVEL FUNCTIONS — DecodeString, EncodeString, transforms
// ============================================================================

// --- Round-trip tests (Encode → Decode → original) ---

TEST(CodeTableConverter, RoundTrip_VNI) {
    std::wstring original = L"Vi\x1EC7t Nam \x0111\x1EB9p";  // Việt Nam đẹp
    auto encoded = CodeTableConverter::EncodeString(original, CT::VNIWindows);
    auto decoded = CodeTableConverter::DecodeString(encoded, CT::VNIWindows);
    EXPECT_EQ(decoded, original);
}

TEST(CodeTableConverter, RoundTrip_Compound) {
    std::wstring original = L"Vi\x1EC7t Nam \x0111\x1EB9p";
    auto encoded = CodeTableConverter::EncodeString(original, CT::UnicodeCompound);
    auto decoded = CodeTableConverter::DecodeString(encoded, CT::UnicodeCompound);
    EXPECT_EQ(decoded, original);
}

TEST(CodeTableConverter, RoundTrip_CP1258) {
    std::wstring original = L"Vi\x1EC7t Nam \x0111\x1EB9p";
    auto encoded = CodeTableConverter::EncodeString(original, CT::VietnameseLocale);
    auto decoded = CodeTableConverter::DecodeString(encoded, CT::VietnameseLocale);
    EXPECT_EQ(decoded, original);
}

TEST(CodeTableConverter, RoundTrip_Unicode) {
    std::wstring original = L"Vi\x1EC7t Nam \x0111\x1EB9p";
    auto encoded = CodeTableConverter::EncodeString(original, CT::Unicode);
    auto decoded = CodeTableConverter::DecodeString(encoded, CT::Unicode);
    EXPECT_EQ(decoded, original);
}

TEST(CodeTableConverter, RoundTrip_TCVN3_Lowercase) {
    // TCVN3 is lossy for case on toned vowels — lowercase round-trips correctly
    std::wstring original = L"vi\x1EC7t nam \x0111\x1EB9p";  // việt nam đẹp
    auto encoded = CodeTableConverter::EncodeString(original, CT::TCVN3);
    auto decoded = CodeTableConverter::DecodeString(encoded, CT::TCVN3);
    EXPECT_EQ(decoded, original);
}

// --- Specific decode tests ---

TEST(CodeTableConverter, Decode_TCVN3_Known) {
    // 0xAE = đ, 0xA9 = â
    std::wstring tcvn3Input = {static_cast<wchar_t>(0xAE), static_cast<wchar_t>(0xA9)};
    auto decoded = CodeTableConverter::DecodeString(tcvn3Input, CT::TCVN3);
    EXPECT_EQ(decoded[0], L'\x0111');  // đ
    EXPECT_EQ(decoded[1], L'\x00E2');  // â
}

TEST(CodeTableConverter, Decode_VNI_Known) {
    // VNI 'a' + 0xF9 = á (U+00E1)
    std::wstring vniInput = {L'a', static_cast<wchar_t>(0xF9)};
    auto decoded = CodeTableConverter::DecodeString(vniInput, CT::VNIWindows);
    EXPECT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0], L'\x00E1');  // á
}

TEST(CodeTableConverter, Decode_Compound_Known) {
    // Compound: 'a' + U+0301 (combining acute) = á
    std::wstring compInput = {L'a', static_cast<wchar_t>(0x0301)};
    auto decoded = CodeTableConverter::DecodeString(compInput, CT::UnicodeCompound);
    EXPECT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0], L'\x00E1');  // á
}

TEST(CodeTableConverter, Decode_ASCII_Passthrough) {
    std::wstring ascii = L"Hello World 123";
    EXPECT_EQ(CodeTableConverter::DecodeString(ascii, CT::VNIWindows), ascii);
    EXPECT_EQ(CodeTableConverter::DecodeString(ascii, CT::TCVN3), ascii);
    EXPECT_EQ(CodeTableConverter::DecodeString(ascii, CT::UnicodeCompound), ascii);
    EXPECT_EQ(CodeTableConverter::DecodeString(ascii, CT::VietnameseLocale), ascii);
}

// --- EncodeString tests ---

TEST(CodeTableConverter, EncodeString_Unicode_Identity) {
    std::wstring input = L"Vi\x1EC7t Nam";
    EXPECT_EQ(CodeTableConverter::EncodeString(input, CT::Unicode), input);
}

TEST(CodeTableConverter, EncodeString_TCVN3) {
    // "Việt" → V, i, 0xD6, t
    std::wstring input = L"Vi\x1EC7t";
    auto encoded = CodeTableConverter::EncodeString(input, CT::TCVN3);
    EXPECT_EQ(encoded.size(), 4u);
    EXPECT_EQ(encoded[0], L'V');
    EXPECT_EQ(encoded[1], L'i');
    EXPECT_EQ(encoded[2], static_cast<wchar_t>(0xD6));
    EXPECT_EQ(encoded[3], L't');
}

// --- RemoveDiacritics tests ---

TEST(CodeTableConverter, RemoveDiacritics_VietNam) {
    std::wstring input = L"Vi\x1EC7t Nam";  // Việt Nam
    auto result = CodeTableConverter::RemoveDiacritics(input);
    EXPECT_EQ(result, L"Viet Nam");
}

TEST(CodeTableConverter, RemoveDiacritics_dep) {
    std::wstring input = L"\x0111\x1EB9p";  // đẹp
    auto result = CodeTableConverter::RemoveDiacritics(input);
    EXPECT_EQ(result, L"dep");
}

TEST(CodeTableConverter, RemoveDiacritics_Mixed) {
    std::wstring input = L"T\x00F4i y\x00EAu Vi\x1EC7t Nam";  // Tôi yêu Việt Nam
    auto result = CodeTableConverter::RemoveDiacritics(input);
    EXPECT_EQ(result, L"Toi yeu Viet Nam");
}

TEST(CodeTableConverter, RemoveDiacritics_AllModifiers) {
    // â ă ê ô ơ ư đ → a a e o o u d
    std::wstring input = L"\x00E2\x0103\x00EA\x00F4\x01A1\x01B0\x0111";
    auto result = CodeTableConverter::RemoveDiacritics(input);
    EXPECT_EQ(result, L"aaeooud");
}

TEST(CodeTableConverter, RemoveDiacritics_Uppercase) {
    // Â Ă Ê Ô Ơ Ư Đ → A A E O O U D
    std::wstring input = L"\x00C2\x0102\x00CA\x00D4\x01A0\x01AF\x0110";
    auto result = CodeTableConverter::RemoveDiacritics(input);
    EXPECT_EQ(result, L"AAEOOUD");
}

TEST(CodeTableConverter, RemoveDiacritics_ASCII_Passthrough) {
    std::wstring input = L"Hello World 123!";
    EXPECT_EQ(CodeTableConverter::RemoveDiacritics(input), input);
}

// --- ToUpper / ToLower tests ---

TEST(CodeTableConverter, ToUpper_Vietnamese) {
    std::wstring input = L"vi\x1EC7t nam \x0111\x1EB9p";  // việt nam đẹp
    auto result = CodeTableConverter::ToUpper(input);
    EXPECT_EQ(result, L"VI\x1EC6T NAM \x0110\x1EB8P");  // VIỆT NAM ĐẸP
}

TEST(CodeTableConverter, ToLower_Vietnamese) {
    std::wstring input = L"VI\x1EC6T NAM \x0110\x1EB8P";  // VIỆT NAM ĐẸP
    auto result = CodeTableConverter::ToLower(input);
    EXPECT_EQ(result, L"vi\x1EC7t nam \x0111\x1EB9p");  // việt nam đẹp
}

TEST(CodeTableConverter, ToUpper_ASCII) {
    EXPECT_EQ(CodeTableConverter::ToUpper(L"hello"), L"HELLO");
}

TEST(CodeTableConverter, ToLower_ASCII) {
    EXPECT_EQ(CodeTableConverter::ToLower(L"HELLO"), L"hello");
}

// --- ToSentenceCase tests ---

TEST(CodeTableConverter, ToSentenceCase_Simple) {
    auto result = CodeTableConverter::ToSentenceCase(L"hello world. goodbye world.");
    EXPECT_EQ(result, L"Hello world. Goodbye world.");
}

TEST(CodeTableConverter, ToSentenceCase_AfterNewline) {
    auto result = CodeTableConverter::ToSentenceCase(L"hello.\nworld");
    EXPECT_EQ(result, L"Hello.\nWorld");
}

TEST(CodeTableConverter, ToSentenceCase_Vietnamese) {
    // "việt nam. đẹp lắm!" → "Việt nam. Đẹp lắm!"
    std::wstring input = L"vi\x1EC7t nam. \x0111\x1EB9p l\x1EAFm!";
    auto result = CodeTableConverter::ToSentenceCase(input);
    EXPECT_EQ(result, L"Vi\x1EC7t nam. \x0110\x1EB9p l\x1EAFm!");
}

TEST(CodeTableConverter, ToSentenceCase_Empty) {
    EXPECT_EQ(CodeTableConverter::ToSentenceCase(L""), L"");
}

TEST(CodeTableConverter, ToSentenceCase_FromUppercase) {
    EXPECT_EQ(CodeTableConverter::ToSentenceCase(L"HELLO WORLD"), L"Hello world");
}

TEST(CodeTableConverter, ToSentenceCase_Vietnamese_Uppercase) {
    // "VIỆT NAM ĐẸP" → "Việt nam đẹp"
    std::wstring input = L"VI\x1EC6T NAM \x0110\x1EB8P";
    auto result = CodeTableConverter::ToSentenceCase(input);
    EXPECT_EQ(result, L"Vi\x1EC7t nam \x0111\x1EB9p");
}

TEST(CodeTableConverter, ToSentenceCase_MixedPunctuation) {
    EXPECT_EQ(CodeTableConverter::ToSentenceCase(L"HELLO.\nWORLD"), L"Hello.\nWorld");
}

// --- ToTitleCase tests ---

TEST(CodeTableConverter, ToTitleCase_Simple) {
    auto result = CodeTableConverter::ToTitleCase(L"hello world test");
    EXPECT_EQ(result, L"Hello World Test");
}

TEST(CodeTableConverter, ToTitleCase_Vietnamese) {
    std::wstring input = L"vi\x1EC7t nam \x0111\x1EB9p";  // việt nam đẹp
    auto result = CodeTableConverter::ToTitleCase(input);
    EXPECT_EQ(result, L"Vi\x1EC7t Nam \x0110\x1EB9p");
}

TEST(CodeTableConverter, ToTitleCase_Empty) {
    EXPECT_EQ(CodeTableConverter::ToTitleCase(L""), L"");
}

TEST(CodeTableConverter, ToTitleCase_FromUppercase) {
    EXPECT_EQ(CodeTableConverter::ToTitleCase(L"HELLO WORLD"), L"Hello World");
}

TEST(CodeTableConverter, ToTitleCase_Vietnamese_Uppercase) {
    // "ĐÂY LÀ TEST" → "Đây Là Test"
    std::wstring input = L"\x0110\x00C2Y L\x00C0 TEST";
    auto result = CodeTableConverter::ToTitleCase(input);
    EXPECT_EQ(result, L"\x0110\x00E2y L\x00E0 Test");
}

// --- Cross-table conversion tests ---

TEST(CodeTableConverter, CrossTable_Unicode_to_VNI_to_Unicode) {
    std::wstring original = L"\x00E1\x00E0\x1EA3\x00E3\x1EA1";  // á à ả ã ạ
    auto vni = CodeTableConverter::EncodeString(original, CT::VNIWindows);
    auto back = CodeTableConverter::DecodeString(vni, CT::VNIWindows);
    EXPECT_EQ(back, original);
}

TEST(CodeTableConverter, CrossTable_VNI_to_CP1258) {
    // Encode to VNI, decode, encode to CP1258, decode — should round-trip
    std::wstring original = L"Vi\x1EC7t Nam";
    auto vniEncoded = CodeTableConverter::EncodeString(original, CT::VNIWindows);
    auto unicode = CodeTableConverter::DecodeString(vniEncoded, CT::VNIWindows);
    auto cp1258 = CodeTableConverter::EncodeString(unicode, CT::VietnameseLocale);
    auto finalResult = CodeTableConverter::DecodeString(cp1258, CT::VietnameseLocale);
    EXPECT_EQ(finalResult, original);
}

}  // namespace
}  // namespace CodeTableConverterTests
}  // namespace NextKey
