// tests/engine/QuickStartConsonantRuleTest.cpp
//
// W7.4 — verify QuickStartConsonantRule metadata + delegation, and
// integration through the real TypingEngine for 0a / 0a-cont / 0b sub-blocks.
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "core/engine/rule/EngineRuleContext.h"
#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleRegistry.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IQuickConsonantExecutor.h"
#include "core/engine/rule/IToneExecutor.h"
#include "core/engine/rule/QuickStartConsonantRule.h"
#include "core/engine/rule/ToneRule.h"
#include "core/pipeline/GateMask.h"

using namespace NextKey;
using namespace NextKey::EngineRule;

namespace {

class CountingQuick final : public IQuickConsonantExecutor {
public:
    int startCalls_ = 0;
    int endCalls_ = 0;
    Result startResult_ = Result::Pass;
    bool endResult_ = false;

    [[nodiscard]] Result HandleQuickStartConsonant(wchar_t, wchar_t, bool) override {
        ++startCalls_;
        return startResult_;
    }
    [[nodiscard]] bool HandleQuickEndConsonant(wchar_t, wchar_t, bool) override {
        ++endCalls_;
        return endResult_;
    }
};

class CountingTone final : public IToneExecutor {
public:
    int calls_ = 0;
    [[nodiscard]] bool HandleToneFsm(TypingAction, wchar_t, wchar_t, bool) override {
        ++calls_;
        return true;
    }
};

EngineRuleContext makeCtx(const std::vector<CharState>& states,
                          const std::vector<wchar_t>& raw,
                          const TypingConfig& cfg,
                          wchar_t keyChar = L'a') {
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

TEST(QuickStartConsonantRule, Metadata_PreClassifyPrio5_RequiresNone) {
    CountingQuick exec;
    QuickStartConsonantRule rule(exec);
    EXPECT_EQ(rule.RulePhase(), Phase::PreClassify);
    EXPECT_EQ(rule.Priority(), 5);
    EXPECT_EQ(rule.Requires(), NextKey::Pipeline::GateMask{0u});
}

TEST(QuickStartConsonantRule, Apply_DelegatesPass) {
    CountingQuick exec;
    exec.startResult_ = Result::Pass;
    QuickStartConsonantRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(states, raw, cfg);
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Pass);
    EXPECT_EQ(exec.startCalls_, 1);
}

TEST(QuickStartConsonantRule, Apply_DelegatesVeto) {
    CountingQuick exec;
    exec.startResult_ = Result::Veto;
    QuickStartConsonantRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(states, raw, cfg);
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Veto);
    EXPECT_EQ(exec.startCalls_, 1);
}

TEST(QuickStartConsonantRule, Integration_f_to_ph_AtWordStart) {
    TypingConfig cfg;
    cfg.quickStartConsonant = true;
    TypingEngine engine(cfg);
    engine.PushChar(L'f');
    EXPECT_EQ(engine.Count(), 2u);
    EXPECT_EQ(engine.Peek(), L"ph");
}

TEST(QuickStartConsonantRule, Integration_j_then_t_UndoesQuickStart) {
    // j → "gi"; then 't' is not a vowel → undo to "jt".
    TypingConfig cfg;
    cfg.quickStartConsonant = true;
    TypingEngine engine(cfg);
    engine.PushChar(L'j');
    EXPECT_EQ(engine.Peek(), L"gi");
    engine.PushChar(L't');
    EXPECT_EQ(engine.Peek(), L"jt");
}

TEST(QuickStartConsonantRule, Integration_cc_to_ch_MidWord) {
    TypingConfig cfg;
    cfg.quickConsonant = true;
    TypingEngine engine(cfg);
    engine.PushChar(L'c');
    engine.PushChar(L'c');
    EXPECT_EQ(engine.Peek(), L"ch");
    EXPECT_TRUE(engine.HasActiveQuickConsonant());
}

TEST(QuickStartConsonantRule, Integration_uu_to_uo_horn) {
    // l u u → l ư ơ
    TypingConfig cfg;
    cfg.quickConsonant = true;
    TypingEngine engine(cfg);
    engine.PushChar(L'l');
    engine.PushChar(L'u');
    engine.PushChar(L'u');
    EXPECT_EQ(engine.Peek(), L"lươ");
}

TEST(QuickStartConsonantRule, RegistryOrder_PreClassifyOnly) {
    // PreClassify dispatch invokes start-rule but not the PostClassify
    // ToneRule mock — proves phase isolation.
    CountingQuick quickExec;
    CountingTone toneExec;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<QuickStartConsonantRule>(quickExec));
    reg.Register(std::make_unique<ToneRule>(toneExec));

    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(states, raw, cfg, L'f');
    (void)reg.DispatchAtPhase(Phase::PreClassify, ctx, engine);
    EXPECT_EQ(quickExec.startCalls_, 1);
    EXPECT_EQ(toneExec.calls_, 0);  // ToneRule lives in PostClassify
}
