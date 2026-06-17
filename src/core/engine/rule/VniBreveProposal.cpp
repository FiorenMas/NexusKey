// src/core/engine/rule/VniBreveProposal.cpp
#include "core/engine/rule/VniBreveProposal.h"

namespace NextKey::EngineRule {

RelocationKind VniBreveProposal::relocationKind() const noexcept {
    // Conditional — same shared backing as VniCircumflex
    // (ProcessVniVowelModifier with gated Pass 1 + unconditional Pass 1.5).
    return RelocationKind::Conditional;
}

bool VniBreveProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleVniBreve(action, keyChar);
}

}  // namespace NextKey::EngineRule
