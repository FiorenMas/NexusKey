// src/core/engine/rule/ToneRule.h
//
// W7.2 PostClassify engine rule (prio 10, requires ToneEscape gate clear).
// Delegates to IToneExecutor::HandleToneFsm. Veto on consumed, Pass on
// fallthrough.
//
// Gate semantics: when escape_.isEscaped() is true, ToneEscapeGate raises
// → registry skips ToneRule → PushChar continues to modifier/regular path.
// The literal-on-escape behavior is preserved by the existing step 3
// (regular char) ProcessChar + UpdateSpellState calls.
#pragma once

#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IEngineRule.h"
#include "core/engine/rule/IToneExecutor.h"
#include "core/pipeline/GateMask.h"

namespace NextKey::EngineRule {

class ToneRule final : public IEngineRule {
public:
    explicit ToneRule(IToneExecutor& exec) noexcept : exec_(exec) {}

    [[nodiscard]] Phase                       RulePhase() const noexcept override { return Phase::PostClassify; }
    [[nodiscard]] int                         Priority()  const noexcept override { return 10; }
    [[nodiscard]] NextKey::Pipeline::GateMask Requires()  const noexcept override {
        return NextKey::Pipeline::GateMaskFor(NextKey::Pipeline::GateId::ToneEscape);
    }

    [[nodiscard]] Result Apply(const EngineRuleContext& ctx,
                                NextKey::TypingEngine& engine) override;

private:
    IToneExecutor& exec_;
};

}  // namespace NextKey::EngineRule
