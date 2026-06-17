// VKey - Vietnamese Phonology Rule-Pack Factory
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Factory + identifier enum for IPhonologyRules instances. Decouples callers
// from concrete rule-pack types so new packs (dialectal, loanword-friendly,
// strict-textbook, …) plug in by:
//
//   1. Implementing IPhonologyRules in a new header,
//   2. Adding a value to PhonologyRulePackId,
//   3. Adding a case to the switch in GetRulePack.
//
// Mirrors the IOutputInjector / WindowClassification → factory pattern used
// in src/app/output/OutputInjectorFactory.{h,cpp} (Sprint 2 T3).
//
// Today only `Default` ships. Closes the T2.1 phonology consolidation sprint
// by giving callers a single hook (`Phonology::GetRulePack(id)`) that future
// features (spell-check overlay, strict/lenient toggle, dialect picker)
// route through without forking validator code.

#pragma once

#include <cstdint>

#include "DefaultPhonologyRules.h"
#include "IPhonologyRules.h"

namespace NextKey {
namespace Phonology {

enum class PhonologyRulePackId : uint8_t {
    Default = 0,
    // Reserved future values (kept commented to avoid YAGNI compile cost):
    // StrictTextbook,
    // LenientLoanword,
    // SouthDialect,
    // NorthDialect,
    // CentralDialect,
};

// Returns the canonical rule-pack instance for the given id. Stateless and
// thread-safe; the returned reference outlives the process. Unknown ids fall
// back to Default — callers in the field never crash on stale ids.
[[nodiscard]] inline const IPhonologyRules& GetRulePack(PhonologyRulePackId id) noexcept {
    switch (id) {
        case PhonologyRulePackId::Default:
            return DefaultPhonologyRules::Default();
    }
    return DefaultPhonologyRules::Default();
}

}  // namespace Phonology
}  // namespace NextKey
