// src/core/pipeline/BackwardEditFeature.h
//
// PostEngine feature that delegates the backward-edit operation to an
// IBackwardEditExecutor (Wave 2 thin wrapper). Wave 3+ will replace the
// delegation with pure-diff intent emission via IntentSink.
#pragma once

#include "core/pipeline/GateMask.h"
#include "core/pipeline/IBackwardEditExecutor.h"
#include "core/pipeline/IFeature.h"
#include "core/pipeline/Result.h"
#include "core/pipeline/Stage.h"

namespace NextKey::Pipeline {

class BackwardEditFeature final : public IFeature {
public:
    explicit BackwardEditFeature(IBackwardEditExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::PostEngine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 10; }
    [[nodiscard]] GateMask Requires()     const noexcept override {
        return GateMaskFor(GateId::EnglishBias);
    }

    [[nodiscard]] Result Try(const KeyContext& ctx, IntentSink& sink) override;

private:
    IBackwardEditExecutor& exec_;
};

}  // namespace NextKey::Pipeline
