// src/core/pipeline/gates/SpellCheckGate.h
//
// Wave 5 — reads IInputEngine::IsEnglishWord() (engine's 3-tier English
// protection bias == HardEnglish). Raised when the engine has classified
// the current buffer as English. Features that re-apply Vietnamese
// transforms can require this gate to defer to user intent.
//
// Holds a reference to HookEngine's `engine_` unique_ptr. The reference is
// stable across config reloads (only the inner pointee is swapped); the
// gate dereferences `engine_->...` on every call.

#pragma once

#include <memory>
#include "core/pipeline/IGate.h"
#include "core/engine/IInputEngine.h"

namespace NextKey::Pipeline {

class SpellCheckGate final : public IGate {
public:
    explicit SpellCheckGate(const std::unique_ptr<NextKey::IInputEngine>& engine) noexcept
        : engine_(engine) {}

    [[nodiscard]] GateId Id() const noexcept override { return GateId::SpellCheck; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override {
        return engine_ && engine_->IsEnglishWord();
    }

private:
    const std::unique_ptr<NextKey::IInputEngine>& engine_;
};

}  // namespace NextKey::Pipeline
