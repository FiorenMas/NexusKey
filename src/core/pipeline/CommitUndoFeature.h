// src/core/pipeline/CommitUndoFeature.h
//
// Wave 3 PreEngine feature (prio 20, no required gates). Delegates the
// keystroke to an ICommitUndoExecutor and maps the CommitUndoOutcome to a
// pipeline Result + a single flow-control intent emitted via IntentSink.
// HookEngine implements the executor (W3.6) by adapting its existing
// HandleCommitUndoFsm — wrap-only strategy per the W3 plan.
#pragma once

#include "core/pipeline/GateMask.h"
#include "core/pipeline/ICommitUndoExecutor.h"
#include "core/pipeline/IFeature.h"
#include "core/pipeline/Result.h"
#include "core/pipeline/Stage.h"

namespace NextKey::Pipeline {

class CommitUndoFeature final : public IFeature {
public:
    explicit CommitUndoFeature(ICommitUndoExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::PreEngine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 20; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return GateMask{0u}; }

    [[nodiscard]] Result Try(const KeyContext& ctx, IntentSink& sink) override;

private:
    ICommitUndoExecutor& exec_;
};

}  // namespace NextKey::Pipeline
