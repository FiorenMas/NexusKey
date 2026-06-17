// src/core/engine/rule/ModifierRule.h
//
// W7.3 PostClassify engine rule (prio 20 — runs after ToneRule prio 10).
// Delegates to IModifierExecutor::HandleModifierAction. Veto on consumed,
// Pass on fallthrough.
//
// Requires=0: modifier processing owns its own gating semantics internally
// (English protection, spell-check, escape handling per modifier kind). No
// W5 gate currently maps cleanly to "skip all modifiers" — that would also
// need a literal-output fallback to preserve current behavior. Defer until
// a real gate consumer emerges; for now, executor handles every action.
#pragma once

#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IEngineRule.h"
#include "core/engine/rule/IModifierExecutor.h"
#include "core/pipeline/GateMask.h"

namespace NextKey::EngineRule {

class ModifierRule final : public IEngineRule {
public:
    explicit ModifierRule(IModifierExecutor& exec) noexcept : exec_(exec) {}

    [[nodiscard]] Phase                       RulePhase() const noexcept override { return Phase::PostClassify; }
    [[nodiscard]] int                         Priority()  const noexcept override { return 20; }
    [[nodiscard]] NextKey::Pipeline::GateMask Requires()  const noexcept override { return NextKey::Pipeline::GateMask{0u}; }

    [[nodiscard]] Result Apply(const EngineRuleContext& ctx,
                                NextKey::TypingEngine& engine) override;

private:
    IModifierExecutor& exec_;
};

}  // namespace NextKey::EngineRule
