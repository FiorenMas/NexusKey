// src/core/pipeline/EscRestoreRawFeature.cpp
#include "core/pipeline/EscRestoreRawFeature.h"

#include "core/pipeline/Intent.h"

namespace NextKey::Pipeline {

Result EscRestoreRawFeature::Try(const KeyContext& ctx, IntentSink& sink) {
    const EscRestoreOutcome outcome = exec_.TryEscRestore(
        ctx.vk, ctx.shift, ctx.ctrl, ctx.alt, ctx.win);
    switch (outcome) {
        case EscRestoreOutcome::Eat:
            sink.Emit(Intents::ConsumeKey{});
            return Result::Handled;
        case EscRestoreOutcome::Fallthrough:
            return Result::Pass;
    }
    return Result::Pass;  // defensive — unreachable for the 2 enumerators above
}

}  // namespace NextKey::Pipeline
