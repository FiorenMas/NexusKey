// src/core/pipeline/EscRestoreRawFeature.h
//
// Wave 4a PreEngine feature (prio 40, no required gates). Delegates the
// keystroke to an IEscRestoreRawExecutor and maps the EscRestoreOutcome to a
// pipeline Result + a single flow-control intent emitted via IntentSink.
// HookEngine implements the executor (W4a.3) by adapting its existing
// TryEscRestoreRaw + hotkey-match logic — wrap-only strategy per the W4a plan.
#pragma once

#include "core/pipeline/GateMask.h"
#include "core/pipeline/IEscRestoreRawExecutor.h"
#include "core/pipeline/IFeature.h"
#include "core/pipeline/Result.h"
#include "core/pipeline/Stage.h"

namespace NextKey::Pipeline {

class EscRestoreRawFeature final : public IFeature {
public:
    explicit EscRestoreRawFeature(IEscRestoreRawExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::PreEngine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 40; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return GateMask{0u}; }

    [[nodiscard]] Result Try(const KeyContext& ctx, IntentSink& sink) override;

private:
    IEscRestoreRawExecutor& exec_;
};

}  // namespace NextKey::Pipeline
