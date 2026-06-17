// src/core/pipeline/IGate.h
//
// Gate predicate — Coordinator queries every registered gate once per keystroke,
// builds the cumulative GateMask of *raised* gates, then filters features
// by Requires() vs raised mask. Features never check gates inside Try().

#pragma once

#include "core/pipeline/GateMask.h"
#include "core/pipeline/KeyContext.h"

namespace NextKey::Pipeline {

class IGate {
public:
    virtual ~IGate() = default;
    [[nodiscard]] virtual GateId Id()                           const noexcept = 0;
    [[nodiscard]] virtual bool   IsRaised(const KeyContext&)    const noexcept = 0;
};

}  // namespace NextKey::Pipeline
