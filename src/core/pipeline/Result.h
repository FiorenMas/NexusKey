// src/core/pipeline/Result.h
//
// Per-feature dispatch outcome. Coordinator reads this to decide whether to
// call the next feature in the stage / next stage / stop entirely.
#pragma once

namespace NextKey::Pipeline {

enum class Result : unsigned char {
    Pass    = 0,  // feature not relevant to this key — try next feature
    Handled = 1,  // feature emitted intents, stop dispatching this stage
    Veto    = 2,  // feature short-circuits — skip remaining stages too
};

}  // namespace NextKey::Pipeline
