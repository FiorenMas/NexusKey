// src/core/engine/rule/EngineRuleRegistry.h
//
// Per-phase registry of IEngineRule plugins. Owns rules by unique_ptr, sorts
// by Priority() (stable) at Register time, dispatches in order. Each
// DispatchAtPhase evaluates engine-local gates once and skips rules whose
// Requires() mask intersects the raised mask.
//
// W7.1 ships the registry with no rules registered. TypingEngine::PushChar
// holds a registry member and calls DispatchAtPhase(Pre|Post) — empty buckets
// are a sub-10ns no-op. W7.2+ registers real rules.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/engine/rule/IEngineRule.h"
#include "core/pipeline/GateMask.h"

namespace NextKey {
class TypingEngine;
}

namespace NextKey::EngineRule {

struct EngineRuleContext;  // fwd — defined in EngineRuleContext.h

class EngineRuleRegistry {
public:
    EngineRuleRegistry();
    ~EngineRuleRegistry();

    EngineRuleRegistry(const EngineRuleRegistry&)            = delete;
    EngineRuleRegistry& operator=(const EngineRuleRegistry&) = delete;

    // Take ownership; sorted by Priority() ascending at Register time
    // (stable_sort preserves registration order for equal priorities).
    void Register(std::unique_ptr<IEngineRule> rule);

    // Per-phase dispatch. Returns the strongest result observed:
    //   Pass    — every rule in bucket returned Pass (or bucket empty / all gated)
    //   Handled — some rule returned Handled; remaining rules in phase skipped
    //   Veto    — some rule returned Veto; caller must return from PushChar
    [[nodiscard]] Result DispatchAtPhase(Phase phase,
                                         const EngineRuleContext& ctx,
                                         TypingEngine& engine);

    // Test introspection.
    [[nodiscard]] std::size_t RuleCountAtPhase(Phase p) const noexcept;
    [[nodiscard]] std::size_t RuleCount() const noexcept;

private:
    [[nodiscard]] NextKey::Pipeline::GateMask EvaluateGates(
        const EngineRuleContext& ctx) const noexcept;

    std::array<std::vector<std::unique_ptr<IEngineRule>>, kPhaseCount> rules_;
};

}  // namespace NextKey::EngineRule
