// src/core/engine/rule/QuickEndConsonantRule.cpp
#include "core/engine/rule/QuickEndConsonantRule.h"

#include "core/engine/rule/EngineRuleContext.h"

namespace NextKey::EngineRule {

Result QuickEndConsonantRule::Apply(const EngineRuleContext& ctx, NextKey::TypingEngine&) {
    // Hot-path pre-guards: skip the vcall for the overwhelming majority of
    // keys that cannot possibly trigger 2c.
    if (!ctx.config.quickEndConsonant) return Result::Pass;
    if (ctx.states.empty() || !ctx.states.back().IsVowel()) return Result::Pass;
    if (ctx.lower != L'g' && ctx.lower != L'h' && ctx.lower != L'k') return Result::Pass;

    return exec_.HandleQuickEndConsonant(ctx.keyChar, ctx.lower, ctx.isUpper)
        ? Result::Veto : Result::Pass;
}

}  // namespace NextKey::EngineRule
