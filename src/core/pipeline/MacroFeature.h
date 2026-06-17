// src/core/pipeline/MacroFeature.h
//
// Wave 4b PreEngine feature (prio 30, no required gates). Delegates the
// keystroke to an IMacroExecutor and maps the MacroOutcome to a pipeline
// Result + a single flow-control intent emitted via IntentSink. HookEngine
// implements the executor (W4b.3) by adapting its existing macro tracking +
// TryExpandMacro logic. Owns macro subsystem entry per design philosophy
// (plugin / single owner).
#pragma once

#include "core/pipeline/GateMask.h"
#include "core/pipeline/IFeature.h"
#include "core/pipeline/IMacroExecutor.h"
#include "core/pipeline/Result.h"
#include "core/pipeline/Stage.h"

namespace NextKey::Pipeline {

class MacroFeature final : public IFeature {
public:
    explicit MacroFeature(IMacroExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] Stage    FeatureStage() const noexcept override { return Stage::PreEngine; }
    [[nodiscard]] int      Priority()     const noexcept override { return 30; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return GateMask{0u}; }

    [[nodiscard]] Result Try(const KeyContext& ctx, IntentSink& sink) override;

private:
    IMacroExecutor& exec_;
};

}  // namespace NextKey::Pipeline
