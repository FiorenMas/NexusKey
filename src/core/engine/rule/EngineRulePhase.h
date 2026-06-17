// src/core/engine/rule/EngineRulePhase.h
//
// Two-phase dispatch slot for engine-internal rules. PreClassify runs before
// TypingAction is computed (where QuickConsonantRule will land in W7.4);
// PostClassify runs after action is resolved (where ToneRule W7.2 and
// ModifierRule W7.3 will land). Mirrors `Pipeline::Stage` upstairs but
// scoped to the engine's PushChar lifecycle.
#pragma once

#include <cstddef>

namespace NextKey::EngineRule {

enum class Phase : unsigned char {
    PreClassify  = 0,
    PostClassify = 1,
};

inline constexpr std::size_t kPhaseCount = 2u;

}  // namespace NextKey::EngineRule
