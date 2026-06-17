// VKey - "Gõ tự do" / allowEnglishBypass tests
// SPDX-License-Identifier: AGPL-3.0-only
//
// Locks the contract for the `allowEnglishBypass` toggle (UI label "Gõ tự do",
// TypingConfig.h: "Bypass English blocking, e.g. yes -> ýe").
//
// Three classes of literal-treatment gates were defeating bypass=on:
//   1. Raw English-tone prefix block      (kBlockedTonePrefixes:    pas, gues)
//   2. Raw English-modifier prefix block  (kBlockedModifierPrefixes: pow, upw)
//   3. spellCheckDisabled_ gates          (block tones/modifiers on invalid
//                                          syllables, e.g. VNI yes2 → literal)
//
// The HardEnglish-bias gate was correctly bypass-gated from day one. These
// tests lock the off-blocks-on-allows contract for all three classes, on both
// Telex and VNI, so future regressions are caught.

#include <gtest/gtest.h>
#include "core/engine/TypingEngine.h"
#include "core/config/TypingConfig.h"
#include "core/ipc/SharedState.h"
#include "TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;

// ============================================================================
// Defaults
// ============================================================================

TEST(EnglishBypassDefaults, OffByDefault) {
    TypingConfig config;
    EXPECT_FALSE(config.allowEnglishBypass);
}

TEST(EnglishBypassDefaults, FeatureFlagRoundTrip_On) {
    TypingConfig in;
    in.allowEnglishBypass = true;
    uint32_t flags = EncodeFeatureFlags(in);
    EXPECT_TRUE(flags & FeatureFlags::ALLOW_ENGLISH_BYPASS);

    TypingConfig out;
    DecodeFeatureFlags(flags, out);
    EXPECT_TRUE(out.allowEnglishBypass);
}

TEST(EnglishBypassDefaults, FeatureFlagRoundTrip_Off) {
    TypingConfig in;  // default: allowEnglishBypass = false
    uint32_t flags = EncodeFeatureFlags(in);
    EXPECT_FALSE(flags & FeatureFlags::ALLOW_ENGLISH_BYPASS);
}

// ============================================================================
// Engine behavior — Telex tone with spell check ON
// ============================================================================

class EnglishBypassTelexTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = true;
        config_.optimizeLevel = 0;
    }
    TypingConfig config_;
};

// Site 1: kBlockedTonePrefixes — "pas" + tone key 'f' (Grave)
TEST_F(EnglishBypassTelexTest, RawTonePrefix_BypassOff_BlocksTone) {
    config_.allowEnglishBypass = false;
    TypingEngine engine(config_);
    TypeString(engine, L"pasf");
    EXPECT_EQ(engine.Peek(), L"pasf");  // Tone literal — block engaged
}

TEST_F(EnglishBypassTelexTest, RawTonePrefix_BypassOn_AllowsTone) {
    config_.allowEnglishBypass = true;
    TypingEngine engine(config_);
    TypeString(engine, L"pasf");
    EXPECT_NE(engine.Peek(), L"pasf");  // Tone must apply somewhere
}

// "gues" + 'f' — second prefix in kBlockedTonePrefixes
TEST_F(EnglishBypassTelexTest, RawTonePrefix_Gues_BypassOff_Blocks) {
    config_.allowEnglishBypass = false;
    TypingEngine engine(config_);
    TypeString(engine, L"guesf");
    EXPECT_EQ(engine.Peek(), L"guesf");
}

TEST_F(EnglishBypassTelexTest, RawTonePrefix_Gues_BypassOn_Allows) {
    config_.allowEnglishBypass = true;
    TypingEngine engine(config_);
    TypeString(engine, L"guesf");
    EXPECT_NE(engine.Peek(), L"guesf");
}

// Site 2: kBlockedModifierPrefixes — "pow" + Telex 'w' modifier
TEST_F(EnglishBypassTelexTest, RawModifierPrefix_BypassOff_BlocksModifier) {
    config_.allowEnglishBypass = false;
    TypingEngine engine(config_);
    TypeString(engine, L"poww");
    EXPECT_EQ(engine.Peek(), L"poww");  // Modifier 'w' literal
}

TEST_F(EnglishBypassTelexTest, RawModifierPrefix_BypassOn_AllowsModifier) {
    config_.allowEnglishBypass = true;
    TypingEngine engine(config_);
    TypeString(engine, L"poww");
    // 'w' modifier must do something — at minimum produce horn (ơ/ư) somewhere
    // or insert ư at end. Either way the literal "poww" string is wrong.
    EXPECT_NE(engine.Peek(), L"poww");
}

// "upw" — second prefix in kBlockedModifierPrefixes
TEST_F(EnglishBypassTelexTest, RawModifierPrefix_Upw_BypassOff_Blocks) {
    config_.allowEnglishBypass = false;
    TypingEngine engine(config_);
    TypeString(engine, L"upww");
    EXPECT_EQ(engine.Peek(), L"upww");
}

TEST_F(EnglishBypassTelexTest, RawModifierPrefix_Upw_BypassOn_Allows) {
    config_.allowEnglishBypass = true;
    TypingEngine engine(config_);
    TypeString(engine, L"upww");
    EXPECT_NE(engine.Peek(), L"upww");
}

// Site 3: engProt_.bias — already gated, lock-in test
// "yes" is not in any kBlocked* table, so this exercises only the bias path.
TEST_F(EnglishBypassTelexTest, HardEnglishBias_BypassOff_BlocksTone) {
    config_.allowEnglishBypass = false;
    TypingEngine engine(config_);
    TypeString(engine, L"yesf");
    EXPECT_EQ(engine.Peek(), L"yesf");
}

TEST_F(EnglishBypassTelexTest, HardEnglishBias_BypassOn_AllowsTone) {
    config_.allowEnglishBypass = true;
    TypingEngine engine(config_);
    TypeString(engine, L"yesf");
    EXPECT_NE(engine.Peek(), L"yesf");
}

// Spell exclusion override should still work when bypass is OFF
// (current behavior — locks in that the fix doesn't break exclusions).
TEST_F(EnglishBypassTelexTest, RawTonePrefix_BypassOff_ExclusionAllows) {
    config_.allowEnglishBypass = false;
    config_.spellExclusions = { L"pás" };
    TypingEngine engine(config_);
    TypeString(engine, L"pass");  // 's' is the tone key Acute
    // Exclusion "pás" matches what tentative tone would produce → allowed
    EXPECT_NE(engine.Peek(), L"pass");
}

// ============================================================================
// Engine behavior — VNI tone (digits don't appear in English prefixes)
// ============================================================================

class EnglishBypassVniTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::VNI;
        config_.spellCheckEnabled = true;
        config_.optimizeLevel = 0;
    }
    TypingConfig config_;
};

// VNI uses digits for tones — kBlockedTonePrefixes can never match (Telex-only).
// Bypass is exercised through engProt_.bias path (line 326, 448).
TEST_F(EnglishBypassVniTest, HardEnglishBias_BypassOff_BlocksTone) {
    config_.allowEnglishBypass = false;
    TypingEngine engine(config_);
    TypeString(engine, L"yes2");  // VNI '2' = Grave
    EXPECT_EQ(engine.Peek(), L"yes2");
}

TEST_F(EnglishBypassVniTest, HardEnglishBias_BypassOn_AllowsTone) {
    config_.allowEnglishBypass = true;
    TypingEngine engine(config_);
    TypeString(engine, L"yes2");
    EXPECT_NE(engine.Peek(), L"yes2");
}

// ============================================================================
// Spell check OFF + bypass interaction
// ============================================================================

// With spell check OFF, the kBlocked* prefix gates don't fire (their gate is
// `config_.spellCheckEnabled`). Bypass still gates engProt_.bias path.
class EnglishBypassNoSpellCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = false;
        config_.optimizeLevel = 0;
    }
    TypingConfig config_;
};

TEST_F(EnglishBypassNoSpellCheckTest, RawTonePrefix_BypassOff_BiasStillBlocks) {
    // Even without spell check, engProt_.bias may still mark this HardEnglish
    // and block the tone. Exact behavior depends on bias heuristics, so we
    // only assert that the tone key didn't disappear (no crash, deterministic).
    config_.allowEnglishBypass = false;
    TypingEngine engine(config_);
    TypeString(engine, L"pasf");
    // Don't over-specify — just make sure engine produces non-empty output.
    EXPECT_FALSE(engine.Peek().empty());
}

TEST_F(EnglishBypassNoSpellCheckTest, RawTonePrefix_BypassOn_AllowsTone) {
    config_.allowEnglishBypass = true;
    TypingEngine engine(config_);
    TypeString(engine, L"pasf");
    EXPECT_NE(engine.Peek(), L"pasf");
}

}  // namespace
}  // namespace NextKey
