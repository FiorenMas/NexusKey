// tests/engine/ModifierRuleTest.cpp
//
// W7.3 — verify ModifierRule short-circuits on non-modifier actions,
// delegates Telex / VNI / UserDefined modifier actions to IModifierExecutor,
// and runs at PostClassify prio 20 (after ToneRule prio 10).
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
#include "core/engine/rule/IToneExecutor.h"
#include "core/engine/rule/ModifierRule.h"
#include "core/engine/rule/ToneRule.h"
#include "core/pipeline/GateMask.h"

using namespace NextKey;
using namespace NextKey::EngineRule;

namespace {

class CountingModifier final : public IModifierExecutor {
public:
    int calls_ = 0;
    bool returnValue_ = true;
    TypingAction lastAction_ = TypingAction::None;

    [[nodiscard]] bool HandleModifierAction(TypingAction action, wchar_t /*keyChar*/,
                                              wchar_t /*lower*/, bool /*isUpper*/) override {
        ++calls_;
        lastAction_ = action;
        return returnValue_;
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

EngineRuleContext makeCtx(TypingAction action,
                          const std::vector<CharState>& states,
                          const std::vector<wchar_t>& raw,
                          const TypingConfig& cfg) {
    return EngineRuleContext{
        .keyChar = L'a', .lower = L'a', .isUpper = false,
        .action = action,
        .spellCheckDisabled = false,
        .allowEnglishBypass = false,
        .escapeActive = false,
        .bias = LanguageBias::Unknown,
        .isVniDigitSeq = false,
        .states = states, .rawInput = raw, .config = cfg,
    };
}

}  // namespace

TEST(ModifierRule, Metadata_PostClassifyPrio20_RequiresNone) {
    CountingModifier exec;
    ModifierRule rule(exec);
    EXPECT_EQ(rule.RulePhase(), Phase::PostClassify);
    EXPECT_EQ(rule.Priority(), 20);
    EXPECT_EQ(rule.Requires(), NextKey::Pipeline::GateMask{0u});
}

TEST(ModifierRule, NonModifierAction_ReturnsPass_DoesNotCallExecutor) {
    CountingModifier exec;
    ModifierRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    for (auto action : {TypingAction::None,
                        TypingAction::ToneAcute,
                        TypingAction::ToneGrave,
                        TypingAction::ClearTone}) {
        auto ctx = makeCtx(action, states, raw, cfg);
        EXPECT_EQ(rule.Apply(ctx, engine), Result::Pass);
    }
    EXPECT_EQ(exec.calls_, 0);
}

TEST(ModifierRule, TelexModifierAction_ExecutorReturnsTrue_VetoesPhase) {
    CountingModifier exec;
    exec.returnValue_ = true;
    ModifierRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(TypingAction::CircumflexA, states, raw, cfg);
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Veto);
    EXPECT_EQ(exec.calls_, 1);
    EXPECT_EQ(exec.lastAction_, TypingAction::CircumflexA);
}

TEST(ModifierRule, VniModifierAction_ExecutorReturnsFalse_FallsThroughAsPass) {
    CountingModifier exec;
    exec.returnValue_ = false;
    ModifierRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(TypingAction::VniBreve, states, raw, cfg);
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Pass);
    EXPECT_EQ(exec.calls_, 1);
}

TEST(ModifierRule, UserDefinedAction_AlsoRouted) {
    CountingModifier exec;
    exec.returnValue_ = true;
    ModifierRule rule(exec);
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    auto ctx = makeCtx(TypingAction::UndoAllMarks, states, raw, cfg);
    EXPECT_EQ(rule.Apply(ctx, engine), Result::Veto);
    EXPECT_EQ(exec.calls_, 1);
}

TEST(ModifierRule, RegistryOrdering_ToneBeforeModifier) {
    // Verify priority-based ordering: ToneRule(10) runs before ModifierRule(20).
    // ToneRule consumes a tone action and vetoes the phase, so ModifierRule
    // should never be called for tone actions.
    CountingTone toneExec;
    CountingModifier modExec;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<ModifierRule>(modExec));
    reg.Register(std::make_unique<ToneRule>(toneExec));

    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    // Tone action → ToneRule vetoes → ModifierRule skipped.
    {
        auto ctx = makeCtx(TypingAction::ToneAcute, states, raw, cfg);
        EXPECT_EQ(reg.DispatchAtPhase(Phase::PostClassify, ctx, engine), Result::Veto);
        EXPECT_EQ(toneExec.calls_, 1);
        EXPECT_EQ(modExec.calls_, 0);
    }
    // Modifier action → ToneRule passes (non-tone) → ModifierRule runs.
    {
        auto ctx = makeCtx(TypingAction::CircumflexA, states, raw, cfg);
        (void)reg.DispatchAtPhase(Phase::PostClassify, ctx, engine);
        EXPECT_EQ(modExec.calls_, 1);
    }
}
