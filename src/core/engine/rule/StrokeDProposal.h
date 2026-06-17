// src/core/engine/rule/StrokeDProposal.h
//
// W8.3 — wraps TypingEngine::HandleStrokeD (Telex `dd` and VNI `d9` →
// đ/Đ). Body stays on TypingEngine (wrap-don't-lift per W7 retro AD-1).
//
// Metadata: relocationKind() == None. Apply path is a pure modifier
// toggle (Modifier::None ↔ Modifier::Stroke); no RelocateToneTo* call.
// Escape branch (Stroke → None) emits the doubled key as literal via
// ProcessChar but that does not relocate tone.
//
// Scope note: HandleVniStroke is a thin forwarder that calls
// HandleStrokeD. The StrokeD ProcessModifier case routes through this
// proposal; VniStroke keeps its own dispatch entry for now to avoid
// expanding W8.3 surface beyond a single re-route.
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class StrokeDProposal final : public ModifierProposal {
public:
    explicit StrokeDProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
