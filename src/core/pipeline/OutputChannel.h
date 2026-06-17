// src/core/pipeline/OutputChannel.h
//
// Production IntentSink — accumulates feature-emitted intents per keystroke,
// then flushes as a single batch. Wave 1 exposes the batch via TakeBatch()
// for tests. Wave 2 will replace TakeBatch with FlushToInjector(IOutputInjector&)
// that issues `Replace(bsCount, text)` + `SendKey(reinjectVk)` in one go.
//
// Pattern B resolution: features never call SendInput; OutputChannel is the
// single serialization point.
#pragma once

#include <vector>
#include "core/pipeline/Intent.h"
#include "core/pipeline/IntentSink.h"

namespace NextKey::Pipeline {

class OutputChannel final : public IntentSink {
public:
    // Reserve small inline capacity so steady-state typing performs zero heap
    // allocation on the LL-hook thread (Rule 11.2). A keystroke emits at most a
    // couple of intents; DrainBatch() ping-pongs capacity between the buffers.
    OutputChannel() { batch_.reserve(4); drained_.reserve(4); }
    ~OutputChannel() = default;

    OutputChannel(const OutputChannel&)            = delete;
    OutputChannel& operator=(const OutputChannel&) = delete;

    void Emit(Intent intent) override;

    // Consumes the current batch. After this call, the channel is empty.
    // (Transfers the buffer out → reallocates on next Emit; off-hot-path /
    // test use. Hot-path callers use DrainBatch instead.)
    [[nodiscard]] std::vector<Intent> TakeBatch();

    // Hot-path drain: swaps the accumulated intents into an internal buffer and
    // returns a view, leaving the emit buffer empty WITH its capacity retained
    // (zero heap traffic on the LL-hook thread, unlike TakeBatch). The returned
    // reference is valid until the next Emit()/DrainBatch(). The channel is empty
    // immediately after the call, so a caller that returns early mid-iteration
    // does not strand intents into the next keystroke.
    [[nodiscard]] const std::vector<Intent>& DrainBatch();

    [[nodiscard]] bool Empty() const noexcept { return batch_.empty(); }

private:
    std::vector<Intent> batch_;
    std::vector<Intent> drained_;  // holds the last DrainBatch() result; capacity persists
};

}  // namespace NextKey::Pipeline
