// src/core/engine/rule/StrokeDProposal.cpp
#include "core/engine/rule/StrokeDProposal.h"

namespace NextKey::EngineRule {

RelocationKind StrokeDProposal::relocationKind() const noexcept {
    return RelocationKind::None;
}

bool StrokeDProposal::tryApply(TypingAction action, wchar_t keyChar) {
    return exec_.HandleStrokeD(action, keyChar);
}

}  // namespace NextKey::EngineRule
