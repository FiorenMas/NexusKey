// src/core/pipeline/IEscRestoreRawExecutor.h
//
// ESC-restore-raw output port. Implemented by HookEngine (Wave 4a) so the
// EscRestoreRawFeature can delegate the hotkey match + raw-input restore
// without coupling to HookEngine's KeyOutcome / HotkeyRegistry types.
// Wave N+ may lift TryEscRestoreRaw's body into the feature for true
// single-owner state.
#pragma once

#include <cstdint>

namespace NextKey::Pipeline {

// Subset of HookEngine::KeyOutcome relevant to ESC restore-raw.
// TryEscRestoreRaw either consumes the key (Eat) or declines (Fallthrough);
// it never asks the caller to pass-through.
enum class EscRestoreOutcome : unsigned char {
    Eat         = 0,  // ESC consumed, raw input restored
    Fallthrough = 1,  // hotkey didn't match, or no live/primed composition
};

class IEscRestoreRawExecutor {
public:
    virtual ~IEscRestoreRawExecutor() = default;

    // Process a candidate VK_ESCAPE-class keystroke. The implementation
    // resolves the hotkey registry (CancelComposition intent) and the
    // live/primed-commit gates internally, then dispatches via injector.
    // Mirrors HandlePreDispatch's inline check at pre-W4a HookEngine.cpp
    // line 1578-1582.
    [[nodiscard]] virtual EscRestoreOutcome TryEscRestore(
        std::uint16_t vkCode,
        bool shift, bool ctrl, bool alt, bool win) = 0;
};

}  // namespace NextKey::Pipeline
