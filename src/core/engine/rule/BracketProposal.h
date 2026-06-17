// src/core/engine/rule/BracketProposal.h
//
// W8.4 — wraps TypingEngine::HandleHornInsert (Telex `[` and `]` →
// direct ơ/ư insertion). Body stays on TypingEngine.
//
// Metadata: relocationKind() == None. Apply path appends a new CharState
// with Modifier::Horn pre-set; escape branch pops it. No RelocateToneTo*
// call.
//
// One proposal covers both HornInsertO (`[`) and HornInsertU (`]`)
// because they share the same body — the action parameter selects the
// base vowel internally (`o` vs `u`).
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class BracketProposal final : public ModifierProposal {
public:
    explicit BracketProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
