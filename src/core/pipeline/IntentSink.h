// src/core/pipeline/IntentSink.h
//
// Sink interface that features write into. Production impl is OutputChannel
// (wraps IOutputInjector). Test impl is RecordingSink that buffers intents.
#pragma once

#include "core/pipeline/Intent.h"

namespace NextKey::Pipeline {

class IntentSink {
public:
    virtual ~IntentSink() = default;
    virtual void Emit(Intent intent) = 0;
};

}  // namespace NextKey::Pipeline
