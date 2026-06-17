// src/core/engine/rule/HornModifierProposal.h
//
// W8.2 — wraps TypingEngine::HandleHornW (Telex `w` modifier, P1-P8) so
// HornW dispatch flows through the proposal layer alongside W8.1's
// AdjacentCircumflexProposal. Body stays on TypingEngine
// (wrap-don't-lift per W7 retro AD-1).
//
// Metadata: relocationKind() == HornVowel. Documents that the apply path
// calls RelocateToneToHornVowel() — a DIFFERENT function from
// RelocateToneToTarget. Tests pin the value so future changes to
// HandleHornW's apply path surface in code review.
//
// W8.2 verdict (probes 2026-05-25): no concrete repro for the structural
// speculate-apply mismatch hypothesised in TODO 2026-05-23. P5/P6 fire
// only for STANDALONE u/o; RelocateToneToHornVowel is a no-op when the
// horn vowel is also the toned vowel. The existing T5 tone-stop-coda
// recovery in WouldBeValidSyllable absorbs the edge cases that would
// otherwise look like a mismatch. WouldBeValidSyllable's `bool` parameter
// stays as-is for W8.2; if a real repro surfaces, the enum extension
// landed by a follow-up commit referencing the failing probe.
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class HornModifierProposal final : public ModifierProposal {
public:
    explicit HornModifierProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
