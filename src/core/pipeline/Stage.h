// src/core/pipeline/Stage.h
//
// Stage enum for Feature Pipeline dispatch ordering.
// Spec: docs/plans/2026-05-22-feature-pipeline-framework-design.md §3
#pragma once

#include <cstddef>

namespace NextKey::Pipeline {

enum class Stage : unsigned char {
    PreEngine  = 0,  // commit-undo, macro, ESC restore — runs BEFORE engine processes the key
    Engine     = 1,  // engine.PushChar (eventually wraps TypingEngine call as a feature)
    PostEngine = 2,  // backward-edit, passthrough — emits intents based on engine result
};

inline constexpr std::size_t kStageCount = 3u;

}  // namespace NextKey::Pipeline
