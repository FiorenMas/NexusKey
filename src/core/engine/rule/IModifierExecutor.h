// src/core/engine/rule/IModifierExecutor.h
//
// Modifier subsystem output port. Implemented by TypingEngine (W7.3) so the
// ModifierRule plugin can delegate Telex / VNI / UserDefined modifier
// dispatch through a single entry point. Mirrors IToneExecutor (W7.2).
//
// HandleModifierAction returns true if a modifier action consumed the key;
// false if no modifier path applied and dispatch should fall through to
// quick-end-consonant / regular character.
#pragma once

#include "core/engine/TypingAction.h"

namespace NextKey::EngineRule {

class IModifierExecutor {
public:
    virtual ~IModifierExecutor() = default;

    [[nodiscard]] virtual bool HandleModifierAction(TypingAction action,
                                                     wchar_t keyChar,
                                                     wchar_t lower,
                                                     bool isUpper) = 0;
};

}  // namespace NextKey::EngineRule
