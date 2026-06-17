// src/core/engine/rule/VniCircumflexProposal.cpp
#include "core/engine/rule/VniCircumflexProposal.h"

namespace NextKey::EngineRule {

RelocationKind VniCircumflexProposal::relocationKind() const noexcept {
    // Conditional — ProcessVniVowelModifier (backing impl) has both gated
    // Pass 1 (`if (needsRelocate) RelocateToneToTarget()` post-W8.5) and
    // unconditional Pass 1.5 (modifier switching) relocate calls.
    return RelocationKind::Conditional;
}

bool VniCircumflexProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleVniCircumflex(action, keyChar);
}

}  // namespace NextKey::EngineRule
