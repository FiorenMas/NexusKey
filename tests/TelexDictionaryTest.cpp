// VKey - Vietnamese Dictionary Coverage Tests
// SPDX-License-Identifier: AGPL-3.0-only
//
// Data-driven tests covering common Vietnamese words.
// Supplements existing scenario-based tests in TelexEngineTest.cpp.
// Goal: catch regressions in everyday vocabulary without touching existing tests.

#include <gtest/gtest.h>
#include "core/engine/TypingEngine.h"
#include "core/config/TypingConfig.h"
#include "TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;

struct WordCase {
    const wchar_t* input;
    const wchar_t* expected;
};

class TelexDictionaryTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = false;
        config_.optimizeLevel = 0;
        config_.modernOrtho = false;
        engine_ = std::make_unique<TypingEngine>(config_);
    }

    void RunCases(const WordCase cases[], size_t count) {
        for (size_t i = 0; i < count; ++i) {
            engine_->Reset();
            TypeString(*engine_, cases[i].input);
            EXPECT_EQ(engine_->Peek(), cases[i].expected)
                << "Case " << i << ": input=\""
                << testing::PrintToString(cases[i].input) << "\"";
        }
    }

    TypingConfig config_;
    std::unique_ptr<TypingEngine> engine_;
};

// ============================================================================
// SINGLE VOWELS WITH ALL 5 TONES
// ============================================================================

static const WordCase kSingleVowelTones[] = {
    // a + tones
    {L"as", L"á"},   {L"af", L"à"},   {L"ar", L"ả"},
    {L"ax", L"ã"},   {L"aj", L"ạ"},
    // e + tones
    {L"es", L"é"},   {L"ef", L"è"},   {L"er", L"ẻ"},
    {L"ex", L"ẽ"},   {L"ej", L"ẹ"},
    // i + tones
    {L"is", L"í"},   {L"if", L"ì"},   {L"ir", L"ỉ"},
    {L"ix", L"ĩ"},   {L"ij", L"ị"},
    // o + tones
    {L"os", L"ó"},   {L"of", L"ò"},   {L"or", L"ỏ"},
    {L"ox", L"õ"},   {L"oj", L"ọ"},
    // u + tones
    {L"us", L"ú"},   {L"uf", L"ù"},   {L"ur", L"ủ"},
    {L"ux", L"ũ"},   {L"uj", L"ụ"},
    // y + tones
    {L"ys", L"ý"},   {L"yf", L"ỳ"},   {L"yr", L"ỷ"},
    {L"yx", L"ỹ"},   {L"yj", L"ỵ"},
};

TEST_F(TelexDictionaryTest, SingleVowelTones) {
    RunCases(kSingleVowelTones, std::size(kSingleVowelTones));
}

// ============================================================================
// MODIFIED VOWELS WITH TONES (â, ă, ê, ô, ơ, ư)
// ============================================================================

static const WordCase kModifiedVowelTones[] = {
    // â
    {L"aas", L"ấ"},  {L"aaf", L"ầ"},  {L"aar", L"ẩ"},
    {L"aax", L"ẫ"},  {L"aaj", L"ậ"},
    // ă
    {L"aws", L"ắ"},  {L"awf", L"ằ"},  {L"awr", L"ẳ"},
    {L"awx", L"ẵ"},  {L"awj", L"ặ"},
    // ê
    {L"ees", L"ế"},  {L"eef", L"ề"},  {L"eer", L"ể"},
    {L"eex", L"ễ"},  {L"eej", L"ệ"},
    // ô
    {L"oos", L"ố"},  {L"oof", L"ồ"},  {L"oor", L"ổ"},
    {L"oox", L"ỗ"},  {L"ooj", L"ộ"},
    // ơ
    {L"ows", L"ớ"},  {L"owf", L"ờ"},  {L"owr", L"ở"},
    {L"owx", L"ỡ"},  {L"owj", L"ợ"},
    // ư
    {L"uws", L"ứ"},  {L"uwf", L"ừ"},  {L"uwr", L"ử"},
    {L"uwx", L"ữ"},  {L"uwj", L"ự"},
};

TEST_F(TelexDictionaryTest, ModifiedVowelTones) {
    RunCases(kModifiedVowelTones, std::size(kModifiedVowelTones));
}

// ============================================================================
// COMMON VIETNAMESE WORDS — EVERYDAY VOCABULARY
// ============================================================================

static const WordCase kCommonWords[] = {
    // Greetings & basics
    {L"vieejt", L"việt"},
    {L"hawf", L"hằ"},

    // People & pronouns
    {L"nguwowif", L"người"},
    {L"chij", L"chị"},
    {L"anh", L"anh"},
    {L"em", L"em"},

    // Common nouns
    {L"nhaf", L"nhà"},
    {L"nuowcs", L"nước"},
    {L"ddaats", L"đất"},
    {L"trowif", L"trời"},
    {L"thowif", L"thời"},

    // Verbs
    {L"laf", L"là"},
    {L"cos", L"có"},
    {L"ddi", L"đi"},
    {L"ddeen", L"đên"},
    {L"ddeens", L"đến"},
    {L"bieets", L"biết"},
    {L"nois", L"nói"},
    {L"lamf", L"làm"},

    // Numbers & time
    {L"moojt", L"một"},
    {L"hai", L"hai"},
    {L"bas", L"bá"},
    {L"boosn", L"bốn"},
    {L"nawm", L"năm"},
    {L"thangs", L"tháng"},
    {L"ngayf", L"ngày"},

    // Food
    {L"phowr", L"phở"},
    {L"bof", L"bò"},
    {L"gaf", L"gà"},
    {L"cowm", L"cơm"},
    {L"banhf", L"bành"},
    {L"mif", L"mì"},

    // Places
    {L"thanhf", L"thành"},
    {L"phoos", L"phố"},
};

TEST_F(TelexDictionaryTest, CommonWords) {
    RunCases(kCommonWords, std::size(kCommonWords));
}

// ============================================================================
// COMPLEX WORDS — DIPHTHONGS, TRIPHTHONGS, CONSONANT CLUSTERS
// ============================================================================

static const WordCase kComplexWords[] = {
    // ươ combinations
    {L"dduwowngf", L"đường"},
    {L"nuowng", L"nương"},
    {L"luowng", L"lương"},
    {L"thuwowng", L"thương"},
    {L"muwowif", L"mười"},
    {L"huwowng", L"hương"},

    // iê combinations
    {L"chieeuf", L"chiều"},
    {L"dieenj", L"diện"},
    {L"lieenj", L"liện"},
    {L"nghieepj", L"nghiệp"},
    {L"thieen", L"thiên"},
    {L"hieenj", L"hiện"},

    // uô combinations
    {L"cuoocj", L"cuộc"},
    {L"quoocs", L"quốc"},
    {L"buooir", L"buổi"},
    {L"muoons", L"muốn"},
    {L"thuoocs", L"thuốc"},

    // Consonant clusters
    {L"nghiax", L"nghĩa"},
    {L"nhaanf", L"nhần"},
    {L"khucs", L"khúc"},
    {L"phangr", L"phảng"},
    {L"trongs", L"tróng"},
    {L"giangs", L"giáng"},

    // gi- prefix words
    {L"gias", L"giá"},
    {L"giaos", L"giáo"},
    {L"giamr", L"giảm"},
    {L"gianf", L"giàn"},
    {L"giuwax", L"giữa"},

    // qu- prefix words
    {L"quas", L"quá"},
    {L"quanr", L"quản"},
    {L"quaans", L"quấn"},
    {L"quyeenf", L"quyền"},
    {L"quys", L"quý"},
};

TEST_F(TelexDictionaryTest, ComplexWords) {
    RunCases(kComplexWords, std::size(kComplexWords));
}

// ============================================================================
// dd → đ IN VARIOUS POSITIONS
// ============================================================================

static const WordCase kStrokeD[] = {
    {L"dd", L"đ"},
    {L"ddi", L"đi"},
    {L"ddef", L"đè"},
    {L"ddo", L"đo"},
    {L"ddos", L"đó"},
    {L"ddowf", L"đờ"},
    {L"ddaangs", L"đấng"},
    {L"ddinh", L"đinh"},
    {L"dduws", L"đứ"},
    {L"dduwowcs", L"đước"},
    {L"ddawcj", L"đặc"},

    // Postfix dd (d at end triggers đ retroactively)
    {L"dinhd", L"đinh"},
    {L"dongd", L"đong"},
};

TEST_F(TelexDictionaryTest, StrokeD) {
    RunCases(kStrokeD, std::size(kStrokeD));
}

// ============================================================================
// w AS VOWEL MODIFIER IN VARIOUS CONTEXTS
// ============================================================================

static const WordCase kWModifier[] = {
    // w → ư (standalone vowel context)
    {L"muw", L"mư"},
    {L"tuws", L"tứ"},
    {L"luwowng", L"lương"},

    // w → ơ (after o)
    {L"mow", L"mơ"},
    {L"low", L"lơ"},
    {L"ngowf", L"ngờ"},

    // w → ă (after a)
    {L"awn", L"ăn"},
    {L"tawm", L"tăm"},
    {L"nawm", L"năm"},

    // w as ư in ươ
    {L"uwow", L"ươ"},
    {L"dduwowngf", L"đường"},
};

TEST_F(TelexDictionaryTest, WModifier) {
    RunCases(kWModifier, std::size(kWModifier));
}

// ============================================================================
// TONE PLACEMENT — COMMON PATTERNS
// ============================================================================

static const WordCase kTonePlacement[] = {
    // Tone on first vowel of falling diphthong
    {L"ais", L"ái"},
    {L"ois", L"ói"},
    {L"uis", L"úi"},
    {L"aos", L"áo"},
    {L"eos", L"éo"},
    {L"aus", L"áu"},

    // Rising diphthong oa/oe — tone on first vowel (o)
    {L"oas", L"óa"},
    {L"oes", L"óe"},
    {L"uys", L"úy"},  // classic: tone on first

    // Triphthongs
    {L"oais", L"oái"},
    {L"uois", L"uói"},
    {L"uoif", L"uòi"},

    // Tone placement in real words
    {L"khoer", L"khỏe"},         // oe: tone on o
    {L"hoaf", L"hòa"},           // oa: tone on o
    {L"toans", L"toán"},
    {L"thueej", L"thuệ"},
};

TEST_F(TelexDictionaryTest, TonePlacement) {
    RunCases(kTonePlacement, std::size(kTonePlacement));
}

// ============================================================================
// UPPERCASE / MIXED CASE
// ============================================================================

static const WordCase kUpperCase[] = {
    {L"VIEETJ", L"VIỆT"},
    {L"DDUWOWNGF", L"ĐƯỜNG"},
    {L"NUOWCS", L"NƯỚC"},
    {L"HAF", L"HÀ"},
    {L"NOOIJ", L"NỘI"},
    {L"Vieetj", L"Việt"},
    {L"Dduwowngf", L"Đường"},
    {L"Nuowcs", L"Nước"},
    {L"Haf", L"Hà"},
};

TEST_F(TelexDictionaryTest, UpperCase) {
    RunCases(kUpperCase, std::size(kUpperCase));
}

}  // namespace
}  // namespace NextKey
