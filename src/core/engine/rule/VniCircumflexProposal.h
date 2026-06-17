// src/core/engine/rule/VniCircumflexProposal.h
//
// W8.5 — wraps TypingEngine::HandleVniCircumflex (VNI `6` modifier →
// â/ê/ô). Body forwards to ProcessVniVowelModifier(Circumflex, key).
//
// Metadata: relocationKind() == Conditional. ProcessVniVowelModifier
// Pass 1 gates RelocateToneToTarget by `needsRelocate` (preState ==
// ValidPrefix, post-W8.5); Pass 1.5 (modifier switching) calls
// RelocateToneToTarget unconditionally. The gated/ungated mix is the
// audit-focal pattern that ad09f15 + c6369dd taught the codebase to
// watch for — declare Conditional so reviewers know to read the body.
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class VniCircumflexProposal final : public ModifierProposal {
public:
    explicit VniCircumflexProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
