// src/core/pipeline/OutputChannel.cpp
#include "core/pipeline/OutputChannel.h"

#include <utility>

namespace NextKey::Pipeline {

void OutputChannel::Emit(Intent intent) {
    batch_.push_back(std::move(intent));
}

std::vector<Intent> OutputChannel::TakeBatch() {
    return std::exchange(batch_, std::vector<Intent>{});
}

const std::vector<Intent>& OutputChannel::DrainBatch() {
    // Swap the accumulated intents into the persistent drained_ buffer. After
    // the swap batch_ holds drained_'s (cleared, capacity-retained) buffer, so
    // the next Emit() reuses that capacity instead of heap-allocating on the
    // LL-hook thread (Rule 11.2). Both buffers ping-pong their capacity, so
    // steady-state typing performs zero allocation here.
    drained_.clear();        // keeps drained_'s capacity
    drained_.swap(batch_);   // drained_ <- intents; batch_ <- empty buffer (capacity retained)
    return drained_;
}

}  // namespace NextKey::Pipeline
