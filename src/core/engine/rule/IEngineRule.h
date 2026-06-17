// src/core/engine/rule/IEngineRule.h
//
// Plugin interface for engine-internal rules (tone, modifier, quick
// consonant). Mirrors Pipeline::IFeature but scoped to TypingEngine's
// PushChar. Each rule declares its phase, priority within phase, and the
// Pipeline::GateMask it needs unblocked. Registry calls Apply() only when
// all required gates are clear.
//
// Engine-layer gates are evaluated by EngineRuleRegistry from engine-internal
// state (LanguageBias, spellCheckDisabled_, escape_) — NOT from the upstairs
// Coordinator's gate snapshot, which is stale by the time PushChar runs.
#pragma once

#include "core/engine/rule/EngineRulePhase.h"
#include "core/engine/rule/EngineRuleResult.h"
#include "core/pipeline/GateMask.h"

namespace NextKey {
class TypingEngine;  // fwd — Apply mutates engine state directly
}

namespace NextKey::EngineRule {

struct EngineRuleContext;  // fwd — defined in EngineRuleContext.h

class IEngineRule {
public:
    virtual ~IEngineRule() = default;

    // Static metadata — must return the same value for the lifetime of the
    // rule instance. Registry caches these at Register() time.
    [[nodiscard]] virtual Phase                       RulePhase() const noexcept = 0;
    [[nodiscard]] virtual int                         Priority()  const noexcept = 0;
    [[nodiscard]] virtual NextKey::Pipeline::GateMask Requires()  const noexcept = 0;

    // Per-keystroke entry. Engine is passed by ref so rules can mutate state
    // (states_, escape_, engProt_, spellCheckDisabled_) directly. W7.1 ships
    // no rules; W7.2+ will either widen friend access or introduce a façade.
    [[nodiscard]] virtual Result Apply(const EngineRuleContext& ctx,
                                       TypingEngine& engine) = 0;
};

}  // namespace NextKey::EngineRule
