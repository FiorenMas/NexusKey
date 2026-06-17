// src/core/pipeline/gates/EnglishBiasGate.h
//
// W2.2 — gate is raised (= blocks BackwardEdit & other Vietnamese-only features)
// when the engine is in English mode (vietnameseMode_ == false).
//
// Holds a const reference to the engine's std::atomic<bool> vietnameseMode_ flag.
// IsRaised() does a single acquire-load on the hot path — matches the existing
// HookEngine::vietnameseMode_.load(acquire) read pattern.

#pragma once

#include <atomic>

#include "core/pipeline/IGate.h"

namespace NextKey::Pipeline {

class EnglishBiasGate final : public IGate {
public:
    explicit EnglishBiasGate(const std::atomic<bool>& vnMode) noexcept
        : vnMode_(vnMode) {}

    [[nodiscard]] GateId Id() const noexcept override { return GateId::EnglishBias; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override {
        return !vnMode_.load(std::memory_order_acquire);
    }

private:
    const std::atomic<bool>& vnMode_;
};

}  // namespace NextKey::Pipeline
