// src/core/engine/rule/QuickEndConsonantRule.h
//
// W7.4 PostClassify engine rule (prio 30 — after Tone:10, Modifier:20).
// Delegates 2c quick-end-consonant (g→ng, h→nh, k→ch after vowel) to
// IQuickConsonantExecutor::HandleQuickEndConsonant.
//
// Apply pre-guards: config toggle off, no vowel tail, or key ≠ g/h/k →
// return Pass without touching the executor. Hot-path optimization
// because QuickConsonantRule runs on every key (unlike Tone/Modifier).
#pragma once

#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IEngineRule.h"
#include "core/engine/rule/IQuickConsonantExecutor.h"
#include "core/pipeline/GateMask.h"

namespace NextKey::EngineRule {

class QuickEndConsonantRule final : public IEngineRule {
public:
    explicit QuickEndConsonantRule(IQuickConsonantExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] Phase                       RulePhase() const noexcept override { return Phase::PostClassify; }
    [[nodiscard]] int                         Priority()  const noexcept override { return 30; }
    [[nodiscard]] NextKey::Pipeline::GateMask Requires()  const noexcept override { return NextKey::Pipeline::GateMask{0u}; }

    [[nodiscard]] Result Apply(const EngineRuleContext& ctx,
                                NextKey::TypingEngine& engine) override;

private:
    IQuickConsonantExecutor& exec_;
};

}  // namespace NextKey::EngineRule
