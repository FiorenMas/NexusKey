// src/core/engine/rule/VniBreveProposal.h
//
// W8.5 — wraps TypingEngine::HandleVniBreve (VNI `8` modifier → ă).
// Body forwards to ProcessVniVowelModifier(Breve, key).
//
// Metadata: relocationKind() == Conditional (same shared backing as
// VniCircumflex — Pass 1 gated relocate + Pass 1.5 unconditional).
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class VniBreveProposal final : public ModifierProposal {
public:
    explicit VniBreveProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
