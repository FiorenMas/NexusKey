// src/core/pipeline/KeyContext.h
//
// Per-keystroke context handed to every feature. Built by Coordinator at the top
// of HandleKey, then passed by const ref through the dispatch loop. POD-like
// — no allocation, no virtual calls in construction, trivially copyable (the
// session is held by non-owning pointer, not a reference, so the struct stays
// assignable/storable should a future feature need to queue it).
#pragma once

#include <cstdint>
#include "core/pipeline/ICompositionSession.h"

namespace NextKey::Pipeline {

struct KeyContext {
    std::uint16_t            vk;            // Win32 VK code, e.g. 'A'=0x41
    wchar_t                  keyChar;       // resolved char (after CapsLock/Shift), 0 if non-alpha
    bool                     shift;
    bool                     capsLock;
    bool                     ctrl;
    bool                     alt;
    bool                     win;

    const ICompositionSession* session;     // const view (non-owning); lifetime tied to caller's session. Never null in production.

    // Optional: re-inject vk after Handled. Populated by the CALLER (HookEngine)
    // before invoking Coordinator::HandleKey — features receive ctx by const ref and
    // can only READ this field, not write it. PostEngine features (e.g. backward
    // edit) read it to emit Intents::Reinject alongside their BS+text intents.
    // 0 = no reinject requested.
    std::uint16_t            reinjectVk = 0;
};

}  // namespace NextKey::Pipeline
