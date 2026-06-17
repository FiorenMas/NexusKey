// tests/engine/ToneRuleTest.cpp
//
// W7.2 — verify ToneRule short-circuits on non-tone actions, delegates to
// IToneExecutor::HandleToneFsm for tone/ClearTone actions, and is gated out
// by ToneEscapeGate when escape is active.
#include <gtest/gtest.h>

#include <vector>

#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "core/engine/rule/EngineRuleContext.h"
#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleRegistry.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IToneExecutor.h"
#include "core/engine/rule/ToneRule.h"
#include "core/pipeline/GateMask.h"

using namespace NextKey;
using namespace NextKey::EngineRule;
using NextKey::Pipeline::GateId;
using NextKey::Pipeline::GateMaskFor;

namespace {

class CountingExecutor final : public IToneExecutor {
public:
    int calls_ = 0;
    bool returnValue_ = true;
    TypingAction lastAction_ = TypingAction::None;
    wchar_t lastKey_ = 0;

    [[nodiscard]] bool HandleToneFsm(TypingAction action, wchar_t keyChar,
                                      wchar_t /*lower*/, bool /*isUpper*/) override {
        ++calls_;
        lastAction_ = action;
        lastKey_ = keyChar;
        return returnValue_;
    }
};

EngineRuleContext makeCtx(TypingAction action,
                          const std::vector<CharState>& states,
                          const std::vector<wchar_t>& raw,
                          const TypingConfig& cfg,
                          bool escapeActive = false) {
    return EngineRuleContext{
        .keyChar = L's', .lower = L's', .isUpper = false,
        .action = action,
        .spellCheckDisabled = false,
        .allowEnglishBypass = false,
        .escapeActive = escapeActive,
        .bias = LanguageBias::Unknown,
        .isVniDigitSeq = false,
        .states = states, .rawInput = raw, .config = cfg,
    };
}

}  // namespace

TEST(ToneRule, Metadata_PostClassifyPrio10_RequiresToneEscape) {
    CountingExecutor exec;
    ToneRule rule(exec);
    EXPECT_EQ(rule.RulePhase(), Phase::PostClassify);
    EXPECT_EQ(rule.Priority(), 10);
    EXPECT_EQ(rule.Requires(), GateMaskFor(GateId::ToneEscape));
}

TEST(ToneRule, NonToneAction_ReturnsPass_DoesNotCallExecutor) {
    CountingExecutor exec;
    ToneRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    for (auto action : {TypingAction::None,
                        TypingAction::CircumflexA,
                        TypingAction::HornW,
                        TypingAction::VniBreve,
                        TypingAction::InsertDStroke}) {
        auto ctx = makeCtx(action, states, raw, cfg);
        EXPECT_EQ(rule.Apply(ctx, engine), Result::Pass);
    }
    EXPECT_EQ(exec.calls_, 0);
}

TEST(ToneRule, ToneAction_ExecutorReturnsTrue_VetoesPhase) {
    CountingExecutor exec;
    exec.returnValue_ = true;
    ToneRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(TypingAction::ToneAcute, states, raw, cfg);
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Veto);
    EXPECT_EQ(exec.calls_, 1);
    EXPECT_EQ(exec.lastAction_, TypingAction::ToneAcute);
}

TEST(ToneRule, ToneAction_ExecutorReturnsFalse_FallsThroughAsPass) {
    CountingExecutor exec;
    exec.returnValue_ = false;
    ToneRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(TypingAction::ToneGrave, states, raw, cfg);
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Pass);
    EXPECT_EQ(exec.calls_, 1);
}

TEST(ToneRule, ClearTone_RoutedThroughExecutor) {
    CountingExecutor exec;
    exec.returnValue_ = true;
    ToneRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(TypingAction::ClearTone, states, raw, cfg);
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Veto);
    EXPECT_EQ(exec.calls_, 1);
    EXPECT_EQ(exec.lastAction_, TypingAction::ClearTone);
}

TEST(ToneRule, RegistryGatesOutWhenToneEscapeActive) {
    // Registered through a real registry — verifies that when escapeActive=true
    // the gate raises and the rule's Apply is never called.
    CountingExecutor exec;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<ToneRule>(exec));

    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    // escapeActive=true → ToneEscape gate raised → ToneRule skipped.
    {
        auto ctx = makeCtx(TypingAction::ToneAcute, states, raw, cfg, /*escapeActive*/ true);
        EXPECT_EQ(reg.DispatchAtPhase(Phase::PostClassify, ctx, engine), Result::Pass);
        EXPECT_EQ(exec.calls_, 0);
    }
    // escapeActive=false → gate clear → ToneRule runs → executor called.
    {
        auto ctx = makeCtx(TypingAction::ToneAcute, states, raw, cfg, /*escapeActive*/ false);
        (void)reg.DispatchAtPhase(Phase::PostClassify, ctx, engine);
        EXPECT_EQ(exec.calls_, 1);
    }
}
