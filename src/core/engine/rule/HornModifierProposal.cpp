// src/core/engine/rule/HornModifierProposal.cpp
#include "core/engine/rule/HornModifierProposal.h"

namespace NextKey::EngineRule {

RelocationKind HornModifierProposal::relocationKind() const noexcept {
    return RelocationKind::HornVowel;
}

bool HornModifierProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleHornW(action, keyChar);
}

}  // namespace NextKey::EngineRule
