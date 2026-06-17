// src/core/engine/rule/QuickStartConsonantRule.cpp
#include "core/engine/rule/QuickStartConsonantRule.h"

#include "core/engine/rule/EngineRuleContext.h"

namespace NextKey::EngineRule {

Result QuickStartConsonantRule::Apply(const EngineRuleContext& ctx, NextKey::TypingEngine&) {
    // Executor handles its own early-outs (config off, etc.) cheaply at the
    // top of the body — Apply stays a single vcall pass-through.
    return exec_.HandleQuickStartConsonant(ctx.keyChar, ctx.lower, ctx.isUpper);
}

}  // namespace NextKey::EngineRule
