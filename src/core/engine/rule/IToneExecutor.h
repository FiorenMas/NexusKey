// src/core/engine/rule/IToneExecutor.h
//
// Tone subsystem output port. Implemented by TypingEngine (W7.2) so the
// ToneRule plugin can route tone/ClearTone actions through a single entry
// point. Mirrors the IMacroExecutor pattern from the HookEngine layer.
//
// HandleToneFsm returns true if the tone action was consumed (caller should
// stop processing this key); false if no tone action applied and the
// dispatcher should fall through to modifier/quick-end-consonant/regular
// paths.
#pragma once

#include "core/engine/TypingAction.h"

namespace NextKey::EngineRule {

class IToneExecutor {
public:
    virtual ~IToneExecutor() = default;

    [[nodiscard]] virtual bool HandleToneFsm(TypingAction action,
                                              wchar_t keyChar,
                                              wchar_t lower,
                                              bool isUpper) = 0;
};

}  // namespace NextKey::EngineRule
