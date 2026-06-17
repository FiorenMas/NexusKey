// src/core/pipeline/GateMask.h
//
// Bitmask of gates a feature requires to be "allowed" (gate.IsBlocked()==false).
// Coordinator evaluates all gates once per keystroke, then filters features by mask
// before calling them. Pattern D resolution in the design doc — features stop
// re-checking gates inside their body; coordinator enforces.
#pragma once

#include <cstdint>

namespace NextKey::Pipeline {

enum class GateId : unsigned char {
    EnglishBias = 0,  // skip transformations when EnglishBias detects English context
    SpellCheck  = 1,  // skip tone/free-marking when current syllable is invalid
    ToneEscape  = 2,  // skip transformations after an escape gesture in the same word

    kCount = 3,
};

using GateMask = std::uint32_t;

[[nodiscard]] inline constexpr GateMask GateMaskFor(GateId id) noexcept {
    return GateMask{1u} << static_cast<unsigned>(id);
}

[[nodiscard]] inline constexpr bool GateMaskHas(GateMask mask, GateId id) noexcept {
    return (mask & GateMaskFor(id)) != 0u;
}

}  // namespace NextKey::Pipeline
