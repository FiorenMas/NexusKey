// VKey - Multi-word Macro Prefix Matcher Tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include "core/MacroPrefix.h"

namespace NextKey {
namespace {

TEST(MacroPrefixTest, EmptySetReturnsFalse) {
    std::unordered_set<std::wstring> keys;
    EXPECT_FALSE(IsSpaceMacroPrefix(L"oc ", keys));
}

TEST(MacroPrefixTest, EmptyCandidateReturnsFalse) {
    std::unordered_set<std::wstring> keys{L"oc om bok"};
    EXPECT_FALSE(IsSpaceMacroPrefix(L"", keys));
}

TEST(MacroPrefixTest, LowercaseKeyFlexiblePrefixHits) {
    std::unordered_set<std::wstring> keys{L"oc om bok"};
    EXPECT_TRUE(IsSpaceMacroPrefix(L"oc ", keys));
    EXPECT_TRUE(IsSpaceMacroPrefix(L"oc om ", keys));
}

TEST(MacroPrefixTest, LowercaseKeyAcceptsTypedUppercase) {
    std::unordered_set<std::wstring> keys{L"oc om bok"};
    EXPECT_TRUE(IsSpaceMacroPrefix(L"OC ", keys));
    EXPECT_TRUE(IsSpaceMacroPrefix(L"Oc Om ", keys));
}

TEST(MacroPrefixTest, UppercaseKeyRejectsTypedLowercase) {
    std::unordered_set<std::wstring> keys{L"Oc Om Bok"};
    EXPECT_FALSE(IsSpaceMacroPrefix(L"oc ", keys));
    EXPECT_FALSE(IsSpaceMacroPrefix(L"oc om ", keys));
}

TEST(MacroPrefixTest, UppercaseKeyAcceptsExactCase) {
    std::unordered_set<std::wstring> keys{L"Oc Om Bok"};
    EXPECT_TRUE(IsSpaceMacroPrefix(L"Oc ", keys));
    EXPECT_TRUE(IsSpaceMacroPrefix(L"Oc Om ", keys));
}

TEST(MacroPrefixTest, NonPrefixReturnsFalse) {
    std::unordered_set<std::wstring> keys{L"hi there"};
    EXPECT_FALSE(IsSpaceMacroPrefix(L"oc ", keys));
}

TEST(MacroPrefixTest, MidCharMismatchReturnsFalse) {
    std::unordered_set<std::wstring> keys{L"oc om bok"};
    EXPECT_FALSE(IsSpaceMacroPrefix(L"oc omm ", keys));
    EXPECT_FALSE(IsSpaceMacroPrefix(L"oc op ", keys));
}

TEST(MacroPrefixTest, CandidateLongerThanKeyReturnsFalse) {
    std::unordered_set<std::wstring> keys{L"oc"};  // no space anyway, but tests length guard
    EXPECT_FALSE(IsSpaceMacroPrefix(L"oc om", keys));
}

TEST(MacroPrefixTest, SelfMatchesAsPrefix) {
    // The full key is trivially a prefix of itself. Caller (HookEngine) gates
    // this separately via exact-match in TryExpandMacro before the save path.
    std::unordered_set<std::wstring> keys{L"oc om bok"};
    EXPECT_TRUE(IsSpaceMacroPrefix(L"oc om bok", keys));
}

TEST(MacroPrefixTest, MultipleKeysFindsAnyMatch) {
    std::unordered_set<std::wstring> keys{L"hi there", L"oc om bok", L"good night"};
    EXPECT_TRUE(IsSpaceMacroPrefix(L"hi ", keys));
    EXPECT_TRUE(IsSpaceMacroPrefix(L"oc ", keys));
    EXPECT_TRUE(IsSpaceMacroPrefix(L"good ", keys));
    EXPECT_FALSE(IsSpaceMacroPrefix(L"bye ", keys));
}

TEST(MacroPrefixTest, MixedCaseKeysRespectedIndependently) {
    std::unordered_set<std::wstring> keys{L"hi there", L"Oc Om Bok"};
    // Lowercase key: flexible
    EXPECT_TRUE(IsSpaceMacroPrefix(L"HI ", keys));
    // Uppercase key: strict — lowercase typing does not match it, but if no
    // other key matches, overall result is false.
    EXPECT_FALSE(IsSpaceMacroPrefix(L"oc ", keys));
    // Typed matches the strict key exactly
    EXPECT_TRUE(IsSpaceMacroPrefix(L"Oc ", keys));
}

}  // namespace
}  // namespace NextKey
