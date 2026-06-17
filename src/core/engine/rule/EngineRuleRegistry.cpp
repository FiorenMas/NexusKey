// src/core/engine/rule/EngineRuleRegistry.cpp
#include "core/engine/rule/EngineRuleRegistry.h"

#include <algorithm>

#include "core/engine/rule/EngineRuleContext.h"  // full def needed for EvaluateGates field reads

namespace NextKey::EngineRule {

using NextKey::Pipeline::GateId;
using NextKey::Pipeline::GateMask;
using NextKey::Pipeline::GateMaskFor;

EngineRuleRegistry::EngineRuleRegistry()  = default;
EngineRuleRegistry::~EngineRuleRegistry() = default;

void EngineRuleRegistry::Register(std::unique_ptr<IEngineRule> rule) {
    const auto phase = rule->RulePhase();
    auto& bucket = rules_[static_cast<std::size_t>(phase)];
    bucket.push_back(std::move(rule));
    std::stable_sort(bucket.begin(), bucket.end(),
                     [](const std::unique_ptr<IEngineRule>& a,
                        const std::unique_ptr<IEngineRule>& b) {
                         return a->Priority() < b->Priority();
                     });
}

// W7.5 retro note: as of 2026-05-23 only ToneEscape has an engine-layer
// consumer (ToneRule). EnglishBias and SpellCheck bits are computed but
// unconsumed at this layer — kept deliberately (AD-9 in W7-retro doc) to
// avoid "remove now / re-add later" churn if a future engine rule declares
// Requires=SpellCheck or Requires=EnglishBias. Cost is ~3 bit-ops per
// dispatch over already-loaded ctx fields. See
// docs/plans/2026-05-23-feature-pipeline-w7-retro.md for the full rationale.
GateMask EngineRuleRegistry::EvaluateGates(const EngineRuleContext& ctx) const noexcept {
    GateMask raised = 0u;
    if (ctx.bias == LanguageBias::HardEnglish && !ctx.allowEnglishBypass) {
        raised |= GateMaskFor(GateId::EnglishBias);
    }
    if (ctx.spellCheckDisabled && ctx.config.spellCheckEnabled && !ctx.allowEnglishBypass) {
        raised |= GateMaskFor(GateId::SpellCheck);
    }
    if (ctx.escapeActive) {
        raised |= GateMaskFor(GateId::ToneEscape);
    }
    return raised;
}

Result EngineRuleRegistry::DispatchAtPhase(Phase phase,
                                           const EngineRuleContext& ctx,
                                           TypingEngine& engine) {
    const GateMask raised = EvaluateGates(ctx);
    auto& bucket = rules_[static_cast<std::size_t>(phase)];
    for (auto& rule : bucket) {
        if ((rule->Requires() & raised) != 0u) continue;
        const Result r = rule->Apply(ctx, engine);
        if (r == Result::Handled) return Result::Handled;
        if (r == Result::Veto)    return Result::Veto;
    }
    return Result::Pass;
}

std::size_t EngineRuleRegistry::RuleCountAtPhase(Phase p) const noexcept {
    return rules_[static_cast<std::size_t>(p)].size();
}

std::size_t EngineRuleRegistry::RuleCount() const noexcept {
    return rules_[0].size() + rules_[1].size();
}

}  // namespace NextKey::EngineRule
