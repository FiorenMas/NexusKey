// src/core/engine/rule/ModifierRule.cpp
#include "core/engine/rule/ModifierRule.h"

#include "core/engine/rule/EngineRuleContext.h"

namespace NextKey::EngineRule {

Result ModifierRule::Apply(const EngineRuleContext& ctx, NextKey::TypingEngine&) {
    // Short-circuit on non-modifier actions to avoid touching the executor
    // for the common path (regular letters, tone actions which ToneRule owns).
    if (!IsTelexModifierAction(ctx.action) &&
        !IsVniModifierAction(ctx.action) &&
        !IsUserDefinedOnlyAction(ctx.action)) {
        return Result::Pass;
    }
    return exec_.HandleModifierAction(ctx.action, ctx.keyChar, ctx.lower, ctx.isUpper)
        ? Result::Veto : Result::Pass;
}

}  // namespace NextKey::EngineRule
