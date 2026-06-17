// src/core/pipeline/gates/ToneEscapeGate.h
//
// Wave 5 — reads IInputEngine::IsToneEscaped() (engine's EscapeState).
// Raised when ANY escape is active (Tone, Circumflex, Horn, Breve, Stroke,
// Modifier). Features that would re-apply the escaped transform should
// require this gate to defer to user intent.
//
// Holds a reference to HookEngine's `engine_` unique_ptr. Same stability
// pattern as SpellCheckGate: reference stays valid across config reloads.

#pragma once

#include <memory>
#include "core/pipeline/IGate.h"
#include "core/engine/IInputEngine.h"

namespace NextKey::Pipeline {

class ToneEscapeGate final : public IGate {
public:
    explicit ToneEscapeGate(const std::unique_ptr<NextKey::IInputEngine>& engine) noexcept
        : engine_(engine) {}

    [[nodiscard]] GateId Id() const noexcept override { return GateId::ToneEscape; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override {
        return engine_ && engine_->IsToneEscaped();
    }

private:
    const std::unique_ptr<NextKey::IInputEngine>& engine_;
};

}  // namespace NextKey::Pipeline
