// src/core/pipeline/IMacroExecutor.h
//
// Macro subsystem output port. Implemented by HookEngine (Wave 4b) so the
// MacroFeature can own both tracking (rawMacroBuffer_ accumulation) and
// expansion (TryExpandMacro dispatch) through a single interface. Wave N+
// may lift the TryExpandMacro body into the feature for true single-owner
// state.
#pragma once

#include <cstdint>

namespace NextKey::Pipeline {

// Mirror of HookEngine macro dispatch results. Differentiates the three
// "key consumed" paths that pre-W4b HandlePreDispatch had:
//   Eat         — return true from ProcessKeyDown (macro expanded, trigger eaten)
//   Pass        — return false (English-mode pass-through, or SkipMacro Esc)
//   Fallthrough — continue to step 3+ (macro subsystem not engaged at all)
//   NoOp        — buffer updated but no commit trigger; continue dispatch
// NoOp vs Fallthrough is a debugging distinction; both let the hook continue.
enum class MacroOutcome : unsigned char {
    Eat         = 0,
    Pass        = 1,
    Fallthrough = 2,
    NoOp        = 3,
};

class IMacroExecutor {
public:
    virtual ~IMacroExecutor() = default;

    // Process a keystroke through the macro subsystem. Implementation reads
    // vnMode, macroEnabled_, macroInEnglish_ atomics + the RCU macroTable
    // snapshot internally. May mutate rawMacroBuffer_, tempMacroOff_,
    // macroCrossCommit_ hook-thread fields. May call existing TryExpandMacro
    // for actual expansion. Modifiers are passed individually; the executor
    // packs them into HotkeyRegistry's mod bitmask if needed.
    [[nodiscard]] virtual MacroOutcome HandleMacro(
        std::uint16_t vkCode,
        bool shift, bool capsLock, bool ctrl, bool alt, bool win) = 0;
};

}  // namespace NextKey::Pipeline
