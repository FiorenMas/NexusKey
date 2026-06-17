// VKey - Vietnamese Phonology Rule-Pack Contract
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// IPhonologyRules — pluggable rule-data provider for Vietnamese phonotactics.
//
// Mirrors the IOutputInjector pattern (src/app/output/IOutputInjector.h):
// abstract contract + `final` default impl + factory/`Default()` singleton.
// Both phonotactics validators (Path 1 CharState in PhonotacticsValidator.cpp,
// Path 2 wstring_view in Phonotactics.cpp) consume rule data through this
// contract instead of reaching into VietnamesePhonologyData.h directly. The
// `final` qualifier on impls enables devirtualization at known static types.
//
// Future rule-pack variants (dialectal Vietnamese, alternate orthographies,
// loanword-friendly modes) plug in by providing another impl + factory entry.
//
// Day-3 wires this into Path 2 (Phonotactics class). Path 1 is unchanged
// for now — its hot path consumes the same data via free functions in
// VietnamesePhonologyData.h. D4 candidate to migrate Path 1 with care.

#pragma once

#include <cstdint>

namespace NextKey {
namespace Phonology {

class IPhonologyRules {
public:
    virtual ~IPhonologyRules() = default;

    // Front-vowel classifier (Day-1 lift). Modifier marks (ê, â) do not
    // change the front/back class — pass the *base* (post-Decompose) wchar.
    [[nodiscard]] virtual bool IsFrontBaseVowel(wchar_t base) const noexcept = 0;

    // Allowed-coda bitmask for a packed vowel-nucleus key (Day-2 lift).
    // Returns 0 when the nucleus has no entry — caller treats as "no
    // restriction → lenient pass-through".
    [[nodiscard]] virtual uint16_t AllowedFinalsForVowelKey(uint32_t vowelKey) const noexcept = 0;

protected:
    IPhonologyRules() = default;
    IPhonologyRules(const IPhonologyRules&) = default;
    IPhonologyRules& operator=(const IPhonologyRules&) = default;
};

}  // namespace Phonology
}  // namespace NextKey
