// src/core/engine/rule/BracketProposal.cpp
#include "core/engine/rule/BracketProposal.h"

namespace NextKey::EngineRule {

RelocationKind BracketProposal::relocationKind() const noexcept {
    return RelocationKind::None;
}

bool BracketProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleHornInsert(action, keyChar);
}

}  // namespace NextKey::EngineRule
