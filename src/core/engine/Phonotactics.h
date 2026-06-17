// VKey - Vietnamese Phonotactics Implementation
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Concrete implementation of IPhonotactics encoding RuleTiengViet rules.
// Reuses VietnameseTables.h tables (kDiphthongClassic/Modern, IsTriphthong)
// where applicable; extends with N1/N2/N3 vowel-coda compatibility,
// closed/pending vowel sets, c/k/qu onset agreement.

#pragma once

#include "IPhonologyRules.h"
#include "IPhonotactics.h"
#include "PhonotacticsValidator.h"  // SyllableState + ValidateSyllableState (same Phonology namespace)

namespace NextKey {
namespace Phonology {

class Phonotactics final : public IPhonotactics {
public:
    /// Default ctor binds to `DefaultPhonologyRules::Default()` — the canonical
    /// Vietnamese rule pack from VietnamesePhonologyData.h.
    Phonotactics() noexcept;

    /// DI ctor: caller injects a custom IPhonologyRules pack. Useful for
    /// dialectal rule packs and unit-test stubs. The reference must outlive
    /// the Phonotactics instance.
    explicit Phonotactics(const IPhonologyRules& rules) noexcept;

    ~Phonotactics() override = default;

    Phonotactics(const Phonotactics&) = delete;
    Phonotactics& operator=(const Phonotactics&) = delete;

    /// Returns the process-wide default Phonotactics instance bound to
    /// DefaultPhonologyRules. Stateless and thread-safe; used as the implicit
    /// dependency for callers that don't inject a custom IPhonotactics
    /// (e.g. TypingEngine's single-arg ctor).
    [[nodiscard]] static const Phonotactics& Default() noexcept;

    [[nodiscard]] size_t TonePosition(
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        bool modernOrtho) const noexcept override;

    [[nodiscard]] bool IsValidSyllable(
        std::wstring_view onset,
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        Tone tone,
        bool modernOrtho) const noexcept override;

    [[nodiscard]] bool CanComplete(
        std::wstring_view partial) const noexcept override;

private:
    const IPhonologyRules& rules_;
};

}  // namespace Phonology
}  // namespace NextKey
