// tests/engine/QuickEndConsonantRuleTest.cpp
//
// W7.4 — verify QuickEndConsonantRule metadata + pre-guards + delegation.
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "core/engine/rule/EngineRuleContext.h"
#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleRegistry.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IModifierExecutor.h"
#include "core/engine/rule/IQuickConsonantExecutor.h"
#include "core/engine/rule/IToneExecutor.h"
#include "core/engine/rule/ModifierRule.h"
#include "core/engine/rule/QuickEndConsonantRule.h"
#include "core/engine/rule/ToneRule.h"
#include "core/pipeline/GateMask.h"

using namespace NextKey;
using namespace NextKey::EngineRule;

namespace {

class CountingQuick final : public IQuickConsonantExecutor {
public:
    int endCalls_ = 0;
    bool endResult_ = true;
    [[nodiscard]] Result HandleQuickStartConsonant(wchar_t, wchar_t, bool) override {
        return Result::Pass;
    }
    [[nodiscard]] bool HandleQuickEndConsonant(wchar_t, wchar_t, bool) override {
        ++endCalls_;
        return endResult_;
    }
};

class StubTone final : public IToneExecutor {
public:
    int calls_ = 0;
    [[nodiscard]] bool HandleToneFsm(TypingAction, wchar_t, wchar_t, bool) override {
        ++calls_;
        return false;
    }
};

class StubModifier final : public IModifierExecutor {
public:
    int calls_ = 0;
    [[nodiscard]] bool HandleModifierAction(TypingAction, wchar_t, wchar_t, bool) override {
        ++calls_;
        return false;
    }
};

EngineRuleContext makeCtx(const std::vector<CharState>& states,
                          const std::vector<wchar_t>& raw,
                          const TypingConfig& cfg,
                          wchar_t keyChar = L'g') {
    return EngineRuleContext{
        .keyChar = keyChar, .lower = (wchar_t)towlower(keyChar),
        .isUpper = static_cast<bool>(iswupper(keyChar)),
        .action = TypingAction::None,
        .spellCheckDisabled = false,
        .allowEnglishBypass = false,
        .escapeActive = false,
        .bias = LanguageBias::Unknown,
        .isVniDigitSeq = false,
        .states = states, .rawInput = raw, .config = cfg,
    };
}

}  // namespace

TEST(QuickEndConsonantRule, Metadata_PostClassifyPrio30_RequiresNone) {
    CountingQuick exec;
    QuickEndConsonantRule rule(exec);
    EXPECT_EQ(rule.RulePhase(), Phase::PostClassify);
    EXPECT_EQ(rule.Priority(), 30);
    EXPECT_EQ(rule.Requires(), NextKey::Pipeline::GateMask{0u});
}

TEST(QuickEndConsonantRule, PreGuard_FeatureOff_NoExecutorCall) {
    CountingQuick exec;
    QuickEndConsonantRule rule(exec);
    TypingConfig cfg;
    cfg.quickEndConsonant = false;
    std::vector<CharState> states;
    states.push_back(CharState{L'a', Modifier::None, Tone::None, false, false, 0, SIZE_MAX});
    std::vector<wchar_t> raw{L'a'};
    TypingEngine engine(cfg);

    auto ctx = makeCtx(states, raw, cfg, L'g');
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Pass);
    EXPECT_EQ(exec.endCalls_, 0);
}

TEST(QuickEndConsonantRule, PreGuard_NoVowelTail_NoExecutorCall) {
    CountingQuick exec;
    QuickEndConsonantRule rule(exec);
    TypingConfig cfg;
    cfg.quickEndConsonant = true;
    std::vector<CharState> states;
    states.push_back(CharState{L'c', Modifier::None, Tone::None, false, false, 0, SIZE_MAX});  // consonant tail
    std::vector<wchar_t> raw{L'c'};
    TypingEngine engine(cfg);

    auto ctx = makeCtx(states, raw, cfg, L'g');
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Pass);
    EXPECT_EQ(exec.endCalls_, 0);
}

TEST(QuickEndConsonantRule, PreGuard_NonGHK_NoExecutorCall) {
    CountingQuick exec;
    QuickEndConsonantRule rule(exec);
    TypingConfig cfg;
    cfg.quickEndConsonant = true;
    std::vector<CharState> states;
    states.push_back(CharState{L'a', Modifier::None, Tone::None, false, false, 0, SIZE_MAX});
    std::vector<wchar_t> raw{L'a'};
    TypingEngine engine(cfg);

    auto ctx = makeCtx(states, raw, cfg, L'm');  // not g/h/k
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Pass);
    EXPECT_EQ(exec.endCalls_, 0);
}

TEST(QuickEndConsonantRule, Integration_g_to_ng_AfterVowel) {
    TypingConfig cfg;
    cfg.quickEndConsonant = true;
    TypingEngine engine(cfg);
    engine.PushChar(L'a');
    engine.PushChar(L'g');
    EXPECT_EQ(engine.Peek(), L"ang");
    EXPECT_EQ(engine.Count(), 3u);
}

TEST(QuickEndConsonantRule, RegistryOrder_AfterToneAndModifier) {
    // Tone(10) → Modifier(20) → QuickEnd(30). Confirm by call sequence.
    StubTone toneExec;
    StubModifier modExec;
    CountingQuick quickExec;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<QuickEndConsonantRule>(quickExec));
    reg.Register(std::make_unique<ToneRule>(toneExec));
    reg.Register(std::make_unique<ModifierRule>(modExec));

    TypingConfig cfg;
    cfg.quickEndConsonant = true;
    std::vector<CharState> states;
    states.push_back(CharState{L'a', Modifier::None, Tone::None, false, false, 0, SIZE_MAX});
    std::vector<wchar_t> raw{L'a'};
    TypingEngine engine(cfg);

    auto ctx = makeCtx(states, raw, cfg, L'g');
    (void)reg.DispatchAtPhase(Phase::PostClassify, ctx, engine);
    // For action=None: ToneRule short-circuits Pass (no call), ModifierRule
    // short-circuits Pass (no call), QuickEnd pre-guards pass + delegates.
    EXPECT_EQ(toneExec.calls_, 0);
    EXPECT_EQ(modExec.calls_, 0);
    EXPECT_EQ(quickExec.endCalls_, 1);
}
