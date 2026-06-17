// src/core/pipeline/MacroFeature.cpp
#include "core/pipeline/MacroFeature.h"

#include "core/pipeline/Intent.h"

namespace NextKey::Pipeline {

Result MacroFeature::Try(const KeyContext& ctx, IntentSink& sink) {
    const MacroOutcome outcome = exec_.HandleMacro(
        ctx.vk, ctx.shift, ctx.capsLock, ctx.ctrl, ctx.alt, ctx.win);
    switch (outcome) {
        case MacroOutcome::Eat:
            sink.Emit(Intents::ConsumeKey{});
            return Result::Handled;
        case MacroOutcome::Pass:
            sink.Emit(Intents::PassThrough{});
            return Result::Veto;
        case MacroOutcome::Fallthrough:
        case MacroOutcome::NoOp:
            return Result::Pass;
    }
    return Result::Pass;  // defensive — unreachable for the 4 enumerators above
}

}  // namespace NextKey::Pipeline
