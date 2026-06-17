// VKey — ComputeShouldAutoCap unit tests (Linux-portable)
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include <string>

#include "core/AutoCapDecision.h"

namespace NextKey {
namespace {

// Helper: invoke ComputeShouldAutoCap with a wide string literal.
[[nodiscard]] bool Decide(const wchar_t* s) {
    return ComputeShouldAutoCap(s, std::wstring(s).size());
}

TEST(AutoCapDecision, EmptyBufferIsDocStart) {
    EXPECT_TRUE(ComputeShouldAutoCap(nullptr, 0));
    EXPECT_TRUE(Decide(L""));
}

TEST(AutoCapDecision, OnlyWhitespaceTreatedAsDocStart) {
    EXPECT_TRUE(Decide(L" "));
    EXPECT_TRUE(Decide(L"   "));
    EXPECT_TRUE(Decide(L"\t"));
    EXPECT_TRUE(Decide(L" \t  "));
}

TEST(AutoCapDecision, NewlineCapsNextChar) {
    EXPECT_TRUE(Decide(L"hello\n"));
    EXPECT_TRUE(Decide(L"hello\r"));
    // Trailing whitespace after newline still caps (start of line + indent).
    EXPECT_TRUE(Decide(L"hello\n   "));
    EXPECT_TRUE(Decide(L"hello\r\n"));
}

TEST(AutoCapDecision, SentencePunctRequiresTrailingSpace) {
    // Sentence end with space → caps.
    EXPECT_TRUE(Decide(L"Hello. "));
    EXPECT_TRUE(Decide(L"Hello? "));
    EXPECT_TRUE(Decide(L"Hello! "));
    EXPECT_TRUE(Decide(L"Hello.   "));
    EXPECT_TRUE(Decide(L"Hello.\t"));

    // Sentence-end punct WITHOUT trailing whitespace (domain case) → no cap.
    EXPECT_FALSE(Decide(L"Hello."));
    EXPECT_FALSE(Decide(L"abc."));    // ".com" / ".vn" pre-caret
    EXPECT_FALSE(Decide(L"abc?"));
    EXPECT_FALSE(Decide(L"abc!"));
}

TEST(AutoCapDecision, MidWordContinuationDoesNotCap) {
    EXPECT_FALSE(Decide(L"hello"));
    EXPECT_FALSE(Decide(L"world "));   // 1 trailing space, but no sentence end
    EXPECT_FALSE(Decide(L"abc def"));
    EXPECT_FALSE(Decide(L"abc def "));
}

TEST(AutoCapDecision, NonSentencePunctDoesNotCap) {
    // Comma, semicolon, colon, parens — not sentence-ending.
    EXPECT_FALSE(Decide(L"abc, "));
    EXPECT_FALSE(Decide(L"abc; "));
    EXPECT_FALSE(Decide(L"abc: "));
    EXPECT_FALSE(Decide(L"abc) "));
}

TEST(AutoCapDecision, NewlineWinsOverPunctRule) {
    // Newline + trailing whitespace → still doc-line start, no punct
    // requirement applies.
    EXPECT_TRUE(Decide(L"abc.\n   "));
    EXPECT_TRUE(Decide(L"abc\n"));
}

TEST(AutoCapDecision, VietnameseTextNoCap) {
    EXPECT_FALSE(Decide(L"Tiếng Việt "));
    EXPECT_TRUE(Decide(L"Hello.  "));
    EXPECT_TRUE(Decide(L"Xin chào.\n"));
}

}  // namespace
}  // namespace NextKey
