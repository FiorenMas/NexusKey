// VKey - Default Vietnamese Phonology Rule Pack
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// DefaultPhonologyRules — canonical Vietnamese rule pack reading from
// VietnamesePhonologyData.h. Singleton via Default(); both phonotactics
// validators (Path 1, Path 2) bind to this when no custom rules are
// injected. Marked `final` so callers see direct devirtualised calls.

#pragma once

#include "IPhonologyRules.h"
#include "VietnamesePhonologyData.h"

namespace NextKey {
namespace Phonology {

class DefaultPhonologyRules final : public IPhonologyRules {
public:
    [[nodiscard]] bool IsFrontBaseVowel(wchar_t base) const noexcept override {
        return ::NextKey::Phonology::IsFrontBaseVowel(base);
    }

    [[nodiscard]] uint16_t AllowedFinalsForVowelKey(uint32_t vowelKey) const noexcept override {
        return ::NextKey::Phonology::GetAllowedFinals(vowelKey);
    }

    [[nodiscard]] static const DefaultPhonologyRules& Default() noexcept {
        static const DefaultPhonologyRules instance;
        return instance;
    }
};

}  // namespace Phonology
}  // namespace NextKey
