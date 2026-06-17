// src/core/pipeline/Intent.h
//
// Output intent — features emit one or more Intents per Handled keystroke.
// Coordinator accumulates intents into the OutputChannel which serializes them
// into a single Win32 SendInput batch (plus injector-specific quirks).
// Features NEVER call SendInput directly. Pattern B resolution in design.
//
// `Intent` is a std::variant alias so callers can use std::holds_alternative
// / std::get / std::visit directly without member-template gymnastics.
#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace NextKey::Pipeline {

namespace Intents {
    struct Backspace { unsigned count; };
    struct Text      { std::wstring text; };
    struct Reinject  { std::uint16_t vk; };
    // Flow-control tag intents — empty structs used by features (e.g. CommitUndoFeature)
    // to signal to the caller (HookEngine) whether to eat the key or pass it to the OS.
    struct ConsumeKey {};
    struct PassThrough {};
}

using Intent = std::variant<Intents::Backspace,
                            Intents::Text,
                            Intents::Reinject,
                            Intents::ConsumeKey,
                            Intents::PassThrough>;

}  // namespace NextKey::Pipeline
