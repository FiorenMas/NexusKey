// src/core/pipeline/HookCompositionSession.h
//
// Concrete ICompositionSession that wraps three caller-owned wstring_views
// (previous-rendered, engine-rendered, raw-input). Constructed per-keystroke
// at the HookEngine call-site; does NOT own the underlying buffers — caller
// must keep them alive for the duration of Coordinator::HandleKey.
#pragma once

#include "core/pipeline/ICompositionSession.h"

#include <string_view>

namespace NextKey::Pipeline {

class HookCompositionSession final : public ICompositionSession {
public:
    HookCompositionSession(std::wstring_view prevRendered,
                           std::wstring_view engineRendered,
                           std::wstring_view rawInput) noexcept
        : prev_(prevRendered), eng_(engineRendered), raw_(rawInput) {}

    [[nodiscard]] std::wstring_view PreviousRendered() const noexcept override { return prev_; }
    [[nodiscard]] std::wstring_view EngineRendered()  const noexcept override { return eng_; }
    [[nodiscard]] std::wstring_view RawInput()        const noexcept override { return raw_; }

private:
    std::wstring_view prev_;
    std::wstring_view eng_;
    std::wstring_view raw_;
};

}  // namespace NextKey::Pipeline
