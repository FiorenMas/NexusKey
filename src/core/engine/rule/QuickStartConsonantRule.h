// src/core/engine/rule/QuickStartConsonantRule.h
//
// W7.4 PreClassify engine rule (prio 5 — first in PreClassify). Delegates
// to IQuickConsonantExecutor::HandleQuickStartConsonant which covers:
//   0a      — quick start consonant (f→ph, j→gi, w→qu) at word start
//   0a-cont — undo quick start on non-vowel follow-up
//   0b      — mid-word cc→ch family + uu→ươ
//
// Requires=0: code read confirms no quick-consonant sub-block consults
// spellCheckDisabled_ / engProt_.bias / escape_.isEscaped(). Truthful
// end-shape; W5 SpellCheckGate stays unconsumed at engine layer (W7.5
// retro decides its fate).
#pragma once

#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IEngineRule.h"
#include "core/engine/rule/IQuickConsonantExecutor.h"
#include "core/pipeline/GateMask.h"

namespace NextKey::EngineRule {

class QuickStartConsonantRule final : public IEngineRule {
public:
    explicit QuickStartConsonantRule(IQuickConsonantExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] Phase                       RulePhase() const noexcept override { return Phase::PreClassify; }
    [[nodiscard]] int                         Priority()  const noexcept override { return 5; }
    [[nodiscard]] NextKey::Pipeline::GateMask Requires()  const noexcept override { return NextKey::Pipeline::GateMask{0u}; }

    [[nodiscard]] Result Apply(const EngineRuleContext& ctx,
                                NextKey::TypingEngine& engine) override;

private:
    IQuickConsonantExecutor& exec_;
};

}  // namespace NextKey::EngineRule
