// tests/engine/EngineRuleRegistryTest.cpp
//
// W7.1 — verify EngineRuleRegistry dispatches IEngineRule plugins in phase/
// priority order, honours Handled/Veto stop semantics, and skips rules whose
// Requires() mask intersects the engine-local gate mask. Empty registry is a
// no-op.
//
// Mock rules don't touch TypingEngine state. The engine argument is captured
// by reference but never dereferenced for state mutation in these tests.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "core/engine/rule/EngineRuleContext.h"
#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleRegistry.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IEngineRule.h"
#include "core/pipeline/GateMask.h"

using namespace NextKey;
using namespace NextKey::EngineRule;
using NextKey::Pipeline::GateId;
using NextKey::Pipeline::GateMask;
using NextKey::Pipeline::GateMaskFor;

namespace {

class TaggingRule final : public IEngineRule {
public:
    TaggingRule(std::string tag, Phase phase, int prio, GateMask requires_,
                Result result, std::vector<std::string>& log)
        : tag_{std::move(tag)}, phase_{phase}, prio_{prio},
          requires_{requires_}, result_{result}, log_{log} {}

    [[nodiscard]] Phase    RulePhase() const noexcept override { return phase_; }
    [[nodiscard]] int      Priority()  const noexcept override { return prio_; }
    [[nodiscard]] GateMask Requires()  const noexcept override { return requires_; }
    [[nodiscard]] Result   Apply(const EngineRuleContext&, TypingEngine&) override {
        log_.push_back(tag_);
        return result_;
    }
private:
    std::string tag_;
    Phase       phase_;
    int         prio_;
    GateMask    requires_;
    Result      result_;
    std::vector<std::string>& log_;
};

// Helper to build a context that doesn't raise any engine-local gate.
EngineRuleContext makeCtx(const std::vector<CharState>& states,
                          const std::vector<wchar_t>& rawInput,
                          const TypingConfig& config,
                          bool escapeActive = false,
                          bool spellCheckDisabled = false,
                          LanguageBias bias = LanguageBias::Unknown) {
    return EngineRuleContext{
        .keyChar = L'a', .lower = L'a', .isUpper = false,
        .action = TypingAction::None,
        .spellCheckDisabled = spellCheckDisabled,
        .allowEnglishBypass = false,
        .escapeActive = escapeActive,
        .bias = bias,
        .isVniDigitSeq = false,
        .states = states, .rawInput = rawInput, .config = config,
    };
}

}  // namespace

TEST(EngineRuleRegistry, EmptyRegistry_DispatchIsNoOp_ReturnsPass) {
    EngineRuleRegistry reg;
    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    auto ctx = makeCtx(states, raw, cfg);
    TypingEngine engine(cfg);

    EXPECT_EQ(reg.DispatchAtPhase(Phase::PreClassify,  ctx, engine), Result::Pass);
    EXPECT_EQ(reg.DispatchAtPhase(Phase::PostClassify, ctx, engine), Result::Pass);
    EXPECT_EQ(reg.RuleCount(), 0u);
    EXPECT_EQ(reg.RuleCountAtPhase(Phase::PreClassify),  0u);
    EXPECT_EQ(reg.RuleCountAtPhase(Phase::PostClassify), 0u);
}

TEST(EngineRuleRegistry, Register_SortsByPriorityAscending_StablePreservesOrder) {
    std::vector<std::string> log;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<TaggingRule>("p30",  Phase::PreClassify, 30, 0u, Result::Pass, log));
    reg.Register(std::make_unique<TaggingRule>("p10",  Phase::PreClassify, 10, 0u, Result::Pass, log));
    reg.Register(std::make_unique<TaggingRule>("p20",  Phase::PreClassify, 20, 0u, Result::Pass, log));
    reg.Register(std::make_unique<TaggingRule>("p20b", Phase::PreClassify, 20, 0u, Result::Pass, log));

    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    auto ctx = makeCtx(states, raw, cfg);
    TypingEngine engine(cfg);
    EXPECT_EQ(reg.DispatchAtPhase(Phase::PreClassify, ctx, engine), Result::Pass);

    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[0], "p10");
    EXPECT_EQ(log[1], "p20");   // registered before p20b
    EXPECT_EQ(log[2], "p20b");
    EXPECT_EQ(log[3], "p30");
}

TEST(EngineRuleRegistry, Dispatch_RunsOnlyMatchingPhase) {
    std::vector<std::string> log;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<TaggingRule>("pre",  Phase::PreClassify,  10, 0u, Result::Pass, log));
    reg.Register(std::make_unique<TaggingRule>("post", Phase::PostClassify, 10, 0u, Result::Pass, log));

    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    auto ctx = makeCtx(states, raw, cfg);
    TypingEngine engine(cfg);

    (void)reg.DispatchAtPhase(Phase::PreClassify, ctx, engine);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "pre");

    (void)reg.DispatchAtPhase(Phase::PostClassify, ctx, engine);
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[1], "post");

    EXPECT_EQ(reg.RuleCountAtPhase(Phase::PreClassify),  1u);
    EXPECT_EQ(reg.RuleCountAtPhase(Phase::PostClassify), 1u);
    EXPECT_EQ(reg.RuleCount(), 2u);
}

TEST(EngineRuleRegistry, Handled_StopsThisPhase_NextPhaseStillRuns) {
    std::vector<std::string> log;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<TaggingRule>("pre-r1", Phase::PreClassify,  10, 0u, Result::Handled, log));
    reg.Register(std::make_unique<TaggingRule>("pre-r2", Phase::PreClassify,  20, 0u, Result::Pass,    log));
    reg.Register(std::make_unique<TaggingRule>("post",   Phase::PostClassify, 10, 0u, Result::Pass,    log));

    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    auto ctx = makeCtx(states, raw, cfg);
    TypingEngine engine(cfg);

    EXPECT_EQ(reg.DispatchAtPhase(Phase::PreClassify,  ctx, engine), Result::Handled);
    EXPECT_EQ(reg.DispatchAtPhase(Phase::PostClassify, ctx, engine), Result::Pass);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "pre-r1");
    EXPECT_EQ(log[1], "post");
}

TEST(EngineRuleRegistry, Veto_StopsThisPhase_CallerObservesVeto) {
    std::vector<std::string> log;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<TaggingRule>("pre-r1", Phase::PreClassify, 10, 0u, Result::Veto, log));
    reg.Register(std::make_unique<TaggingRule>("pre-r2", Phase::PreClassify, 20, 0u, Result::Pass, log));

    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    auto ctx = makeCtx(states, raw, cfg);
    TypingEngine engine(cfg);

    EXPECT_EQ(reg.DispatchAtPhase(Phase::PreClassify, ctx, engine), Result::Veto);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "pre-r1");
}

TEST(EngineRuleRegistry, GateMask_SkipsRulesWithRaisedGate) {
    std::vector<std::string> log;
    EngineRuleRegistry reg;
    // Rule that requires ToneEscape gate to be CLEAR.
    reg.Register(std::make_unique<TaggingRule>(
        "needs-no-escape", Phase::PreClassify, 10,
        GateMaskFor(GateId::ToneEscape), Result::Pass, log));
    // Rule that requires no gates.
    reg.Register(std::make_unique<TaggingRule>(
        "always", Phase::PreClassify, 20, 0u, Result::Pass, log));

    TypingConfig cfg;
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    // escapeActive=true → ToneEscape raised → first rule skipped, second runs.
    {
        auto ctx = makeCtx(states, raw, cfg, /*escapeActive*/ true);
        (void)reg.DispatchAtPhase(Phase::PreClassify, ctx, engine);
        ASSERT_EQ(log.size(), 1u);
        EXPECT_EQ(log[0], "always");
    }

    log.clear();

    // escapeActive=false → both rules run.
    {
        auto ctx = makeCtx(states, raw, cfg, /*escapeActive*/ false);
        (void)reg.DispatchAtPhase(Phase::PreClassify, ctx, engine);
        ASSERT_EQ(log.size(), 2u);
        EXPECT_EQ(log[0], "needs-no-escape");
        EXPECT_EQ(log[1], "always");
    }
}

TEST(EngineRuleRegistry, GateMask_RaisesSpellCheckAndEnglishBiasGates) {
    std::vector<std::string> log;
    EngineRuleRegistry reg;
    reg.Register(std::make_unique<TaggingRule>(
        "needs-spell", Phase::PostClassify, 10,
        GateMaskFor(GateId::SpellCheck), Result::Pass, log));
    reg.Register(std::make_unique<TaggingRule>(
        "needs-en-bias", Phase::PostClassify, 20,
        GateMaskFor(GateId::EnglishBias), Result::Pass, log));

    TypingConfig cfg;
    cfg.spellCheckEnabled = true;  // SpellCheck gate only raises when config is on
    std::vector<CharState> states;
    std::vector<wchar_t> raw;
    TypingEngine engine(cfg);

    // spellCheckDisabled=true + spellCheckEnabled=true → SpellCheck gate raised
    // → first rule skipped. bias=HardEnglish → EnglishBias raised → second skipped.
    {
        auto ctx = makeCtx(states, raw, cfg,
                           /*escapeActive*/ false,
                           /*spellCheckDisabled*/ true,
                           /*bias*/ LanguageBias::HardEnglish);
        (void)reg.DispatchAtPhase(Phase::PostClassify, ctx, engine);
        EXPECT_TRUE(log.empty());
    }

    // Both gates clear → both rules run.
    {
        auto ctx = makeCtx(states, raw, cfg,
                           /*escapeActive*/ false,
                           /*spellCheckDisabled*/ false,
                           /*bias*/ LanguageBias::Vietnamese);
        (void)reg.DispatchAtPhase(Phase::PostClassify, ctx, engine);
        ASSERT_EQ(log.size(), 2u);
        EXPECT_EQ(log[0], "needs-spell");
        EXPECT_EQ(log[1], "needs-en-bias");
    }
}
