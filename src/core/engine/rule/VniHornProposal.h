// src/core/engine/rule/VniHornProposal.h
//
// W8.5 — wraps TypingEngine::HandleVniHorn (VNI `7` modifier → ơ/ư).
// Body handles VNI 7 with uo-pair and uu-pattern logic, then falls
// through to ProcessVniVowelModifier(Horn, key) for the generic case.
//
// Metadata: relocationKind() == Conditional. Body has unconditional
// uo/uu paths AND a generic fallback through ProcessVniVowelModifier
// (which gates Pass 1 by needsRelocate). All relocate calls use
// RelocateToneToTarget — NOT RelocateToneToHornVowel. Despite the
// surface similarity to Telex HornW (W8.2), the relocate function
// differs: Telex Horn uses RelocateToneToHornVowel while VNI Horn uses
// RelocateToneToTarget. Initial W8.5 commit mis-declared HornVowel
// (corrected via d69d081 to TargetTone, then to Conditional via this
// commit when the "Option B" criterion was crisped post-review).
#pragma once

#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"

namespace NextKey::EngineRule {

class VniHornProposal final : public ModifierProposal {
public:
    explicit VniHornProposal(IModifierSubExecutor& exec) noexcept
        : exec_(exec) {}

    [[nodiscard]] RelocationKind relocationKind() const noexcept override;
    [[nodiscard]] bool tryApply(TypingAction action, wchar_t keyChar) override;

private:
    IModifierSubExecutor& exec_;
};

}  // namespace NextKey::EngineRule
