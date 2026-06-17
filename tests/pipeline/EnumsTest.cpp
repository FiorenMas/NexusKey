// tests/pipeline/EnumsTest.cpp
#include <gtest/gtest.h>
#include "core/pipeline/Stage.h"
#include "core/pipeline/Result.h"
#include "core/pipeline/GateMask.h"

using namespace NextKey::Pipeline;

TEST(Stage, ThreeOrderedStages) {
    EXPECT_LT(static_cast<int>(Stage::PreEngine),  static_cast<int>(Stage::Engine));
    EXPECT_LT(static_cast<int>(Stage::Engine),     static_cast<int>(Stage::PostEngine));
}

TEST(Stage, AllStagesCount) {
    EXPECT_EQ(kStageCount, 3u);
}

TEST(Result, ThreeVariants) {
    Result r1 = Result::Pass;
    Result r2 = Result::Handled;
    Result r3 = Result::Veto;
    EXPECT_NE(r1, r2);
    EXPECT_NE(r2, r3);
    EXPECT_NE(r1, r3);
}

TEST(GateMask, EmptyMaskBlocksNothing) {
    GateMask m = 0u;
    EXPECT_FALSE(GateMaskHas(m, GateId::EnglishBias));
}

TEST(GateMask, SetSingleBitAndQuery) {
    GateMask m = GateMaskFor(GateId::EnglishBias);
    EXPECT_TRUE(GateMaskHas(m, GateId::EnglishBias));
    EXPECT_FALSE(GateMaskHas(m, GateId::SpellCheck));
}

TEST(GateMask, ComposeMultiple) {
    GateMask m = GateMaskFor(GateId::EnglishBias) | GateMaskFor(GateId::SpellCheck);
    EXPECT_TRUE(GateMaskHas(m, GateId::EnglishBias));
    EXPECT_TRUE(GateMaskHas(m, GateId::SpellCheck));
    EXPECT_FALSE(GateMaskHas(m, GateId::ToneEscape));
}
