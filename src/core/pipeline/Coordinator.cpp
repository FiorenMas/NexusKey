// src/core/pipeline/Coordinator.cpp
#include "core/pipeline/Coordinator.h"

#include <algorithm>

namespace NextKey::Pipeline {

Coordinator::Coordinator()  = default;
Coordinator::~Coordinator() = default;

void Coordinator::Register(std::unique_ptr<IFeature> feature) {
    const auto stage = feature->FeatureStage();
    auto& bucket = features_[static_cast<std::size_t>(stage)];
    bucket.push_back(std::move(feature));
    // stable_sort preserves registration order when two features share a
    // Priority, so dispatch is deterministic across compilers/runs.
    std::stable_sort(bucket.begin(), bucket.end(),
                     [](const std::unique_ptr<IFeature>& a, const std::unique_ptr<IFeature>& b) {
                         return a->Priority() < b->Priority();
                     });
}

void Coordinator::RegisterGate(std::unique_ptr<IGate> gate) {
    gates_.push_back(std::move(gate));
}

GateMask Coordinator::EvaluateGates(const KeyContext& ctx) const {
    GateMask raised = 0u;
    for (const auto& g : gates_) {
        if (g->IsRaised(ctx)) raised |= GateMaskFor(g->Id());
    }
    return raised;
}

void Coordinator::HandleKey(const KeyContext& ctx, IntentSink& sink) {
    const GateMask raised = EvaluateGates(ctx);

    for (std::size_t s = 0; s < kStageCount; ++s) {
        auto& bucket = features_[s];
        for (auto& feature : bucket) {
            // Skip if any required gate is raised.
            if ((feature->Requires() & raised) != 0u) continue;

            const Result r = feature->Try(ctx, sink);
            if (r == Result::Handled) break;            // stop this stage
            if (r == Result::Veto)    return;           // stop all stages
            // Result::Pass — continue to next feature
        }
    }
}

void Coordinator::HandleKeyAtStage(Stage stage, const KeyContext& ctx, IntentSink& sink) {
    const GateMask raised = EvaluateGates(ctx);
    auto& bucket = features_[static_cast<std::size_t>(stage)];
    for (auto& feature : bucket) {
        // Skip if any required gate is raised.
        if ((feature->Requires() & raised) != 0u) continue;

        const Result r = feature->Try(ctx, sink);
        if (r == Result::Handled) break;            // stop this stage
        if (r == Result::Veto)    return;           // stop this stage (single-stage scope)
        // Result::Pass — continue to next feature in this stage
    }
}

std::size_t Coordinator::FeatureCountAtStage(Stage s) const noexcept {
    return features_[static_cast<std::size_t>(s)].size();
}

std::size_t Coordinator::GateCount() const noexcept {
    return gates_.size();
}

}  // namespace NextKey::Pipeline
