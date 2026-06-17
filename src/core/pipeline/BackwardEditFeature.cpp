// src/core/pipeline/BackwardEditFeature.cpp
#include "core/pipeline/BackwardEditFeature.h"

namespace NextKey::Pipeline {

Result BackwardEditFeature::Try(const KeyContext& ctx, IntentSink& /*sink*/) {
    exec_.ExecuteReplace(ctx.session->EngineRendered(), ctx.reinjectVk);
    return Result::Handled;
}

}  // namespace NextKey::Pipeline
