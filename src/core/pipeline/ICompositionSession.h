// src/core/pipeline/ICompositionSession.h
//
// Read-only view over engine composition state. Features access engine
// data through this interface — never directly via TypingEngine fields.
// Wave 1 ships interface only; Wave 2 wires a concrete impl that wraps
// HookEngine's engine_ + rawInput_ + commitStack_ via const refs.
#pragma once

#include <string_view>

namespace NextKey::Pipeline {

class ICompositionSession {
public:
    virtual ~ICompositionSession() = default;

    // The text currently rendered to the foreground app (post backward-edit
    // sync). Equivalent to today's previousComposition_.
    [[nodiscard]] virtual std::wstring_view PreviousRendered() const noexcept = 0;

    // What the engine would render after processing the current key. Equivalent
    // to today's engine_->Peek() after PushChar. Empty if no engine state.
    [[nodiscard]] virtual std::wstring_view EngineRendered() const noexcept = 0;

    // Raw input chars typed in the current word (pre-transformation).
    // Equivalent to today's rawInput_ in HookEngine. Used by ESC restore-raw,
    // English bias detection, commit-undo replay.
    [[nodiscard]] virtual std::wstring_view RawInput() const noexcept = 0;
};

}  // namespace NextKey::Pipeline
