// src/core/engine/rule/VniHornProposal.cpp
#include "core/engine/rule/VniHornProposal.h"

namespace NextKey::EngineRule {

RelocationKind VniHornProposal::relocationKind() const noexcept {
    // Conditional — HandleVniHorn has unconditional uo/uu paths AND a
    // generic fallback through ProcessVniVowelModifier (gated). All
    // relocate calls use RelocateToneToTarget (NOT RelocateToneToHornVowel
    // despite surface similarity to Telex HornW W8.2).
    return RelocationKind::Conditional;
}

bool VniHornProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleVniHorn(action, keyChar);
}

}  // namespace NextKey::EngineRule
