// src/core/pipeline/IBackwardEditExecutor.h
//
// Backward-edit output port. Implemented by HookEngine (Wave 2) so the
// BackwardEditFeature can delegate the actual text replacement without
// coupling to the full HookEngine class. Wave 3+ will split this into
// pure-diff (feature) + Stage A–E execute (OutputChannel).
#pragma once

#include <cstdint>
#include <string_view>

namespace NextKey::Pipeline {

class IBackwardEditExecutor {
public:
    virtual ~IBackwardEditExecutor() = default;

    // Replace the foreground app's text with `newText`, optionally
    // re-injecting `reinjectVk` first (0 = no reinject).
    virtual void ExecuteReplace(std::wstring_view newText,
                                std::uint16_t reinjectVk) = 0;
};

}  // namespace NextKey::Pipeline
