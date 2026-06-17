// src/core/pipeline/IFeature.h
//
// The plugin interface. Each feature declares its stage, priority, and the
// gate-mask it requires to be unblocked. Coordinator dispatches features in
// (stage, priority) order, skipping any with at least one required gate
// raised. Result tells Coordinator whether to continue, stop this stage, or
// veto remaining stages.
#pragma once

#include "core/pipeline/Stage.h"
#include "core/pipeline/Result.h"
#include "core/pipeline/GateMask.h"
#include "core/pipeline/KeyContext.h"
#include "core/pipeline/IntentSink.h"

namespace NextKey::Pipeline {

class IFeature {
public:
    virtual ~IFeature() = default;

    // Static metadata — must return the same value for the lifetime of the
    // feature instance. Coordinator caches these at Register() time.
    [[nodiscard]] virtual Stage    FeatureStage() const noexcept = 0;
    [[nodiscard]] virtual int      Priority()     const noexcept = 0;
    [[nodiscard]] virtual GateMask Requires()     const noexcept = 0;

    // Per-keystroke entry point. May emit zero or more intents via sink.
    // Coordinator calls Try only when this feature's `Requires()` is satisfied
    // by the current gates evaluation.
    [[nodiscard]] virtual Result Try(const KeyContext& ctx, IntentSink& sink) = 0;
};

}  // namespace NextKey::Pipeline
