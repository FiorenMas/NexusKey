// src/core/engine/rule/AdjacentCircumflexProposal.cpp
#include "core/engine/rule/AdjacentCircumflexProposal.h"

namespace NextKey::EngineRule {

RelocationKind AdjacentCircumflexProposal::relocationKind() const noexcept {
    return RelocationKind::Conditional;
}

bool AdjacentCircumflexProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleAdjacentCircumflex(action, keyChar);
}

}  // namespace NextKey::EngineRule
