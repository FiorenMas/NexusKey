// src/core/engine/rule/AdjacentCircumflexProposal.h
//
// First concrete ModifierProposal (W8.1). Wraps TypingEngine's
// HandleAdjacentCircumflex (both adjacent + free-marking branches) so
// CircumflexA/E/O dispatch can route through the proposal layer while
// the engine body stays put (wrap-don't-lift per W7 retro AD-1).
//
// Single class spans both branches (§0 Option B) because they share the
// same targetBase resolution, the same ShouldRejectModifier /
// WouldBeValidSyllable plumbing, and the same exit (last.mod = Circumflex;
// [optional] RelocateToneToTarget()) — splitting would fragment shared
// state-detection logic without functional benefit.
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class AdjacentCircumflexProposal final : public ModifierProposal {
public:
    explicit AdjacentCircumflexProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    // Adjacent branch is CONDITIONAL on pre-state (ValidPrefix → relocates;
    // Valid → mod-only, no relocate; Invalid → reject). Free-marking branch
    // unconditionally relocates. RelocationKind::Conditional captures the
    // union; auditors must read both branches in HandleAdjacentCircumflex.
    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
