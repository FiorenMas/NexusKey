// src/core/pipeline/CommitUndoFeature.cpp
#include "core/pipeline/CommitUndoFeature.h"

#include "core/pipeline/Intent.h"

namespace NextKey::Pipeline {

Result CommitUndoFeature::Try(const KeyContext& ctx, IntentSink& sink) {
    const CommitUndoOutcome outcome = exec_.HandleCommitUndo(ctx.vk);
    switch (outcome) {
        case CommitUndoOutcome::Eat:
            sink.Emit(Intents::ConsumeKey{});
            return Result::Handled;
        case CommitUndoOutcome::Pass:
            sink.Emit(Intents::PassThrough{});
            return Result::Veto;
        case CommitUndoOutcome::Fallthrough:
            return Result::Pass;
    }
    return Result::Pass;  // defensive — unreachable for the 3 enumerators above
}

}  // namespace NextKey::Pipeline
