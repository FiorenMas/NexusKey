// src/core/pipeline/ICommitUndoExecutor.h
//
// Commit-undo output port. Implemented by HookEngine (Wave 3) so the
// CommitUndoFeature can delegate the FSM decision without coupling to
// HookEngine's KeyOutcome enum (which lives in src/app/system/). Wave N+
// will lift the FSM body into the feature for true single-owner state.
#pragma once

#include <cstdint>

namespace NextKey::Pipeline {

// Mirror of NextKey::KeyOutcome — declared here so the feature stays
// decoupled from HookEngine's enum. HookEngine maps its KeyOutcome to this
// enum at the executor adapter boundary.
enum class CommitUndoOutcome : unsigned char {
    Eat         = 0,   // ProcessKeyDown should return true (key eaten)
    Pass        = 1,   // ProcessKeyDown should return false (pass to OS)
    Fallthrough = 2,   // ProcessKeyDown should continue to step 3+
};

class ICommitUndoExecutor {
public:
    virtual ~ICommitUndoExecutor() = default;

    // Process a keystroke through the commit-undo FSM. The implementation
    // reads vnMode internally (vietnameseMode_ atomic on HookEngine).
    [[nodiscard]] virtual CommitUndoOutcome HandleCommitUndo(
        std::uint16_t vkCode) = 0;
};

}  // namespace NextKey::Pipeline
