// src/core/engine/rule/ToneRule.cpp
#include "core/engine/rule/ToneRule.h"

#include "core/engine/rule/EngineRuleContext.h"

namespace NextKey::EngineRule {

Result ToneRule::Apply(const EngineRuleContext& ctx, NextKey::TypingEngine&) {
    if (!IsToneAction(ctx.action)) return Result::Pass;
    return exec_.HandleToneFsm(ctx.action, ctx.keyChar, ctx.lower, ctx.isUpper)
        ? Result::Veto : Result::Pass;
}

}  // namespace NextKey::EngineRule
