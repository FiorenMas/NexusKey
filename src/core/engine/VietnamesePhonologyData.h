// VKey - Vietnamese Phonology Shared Data
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Single source of truth for Vietnamese phonological rule data consumed by
// the project's two phonotactics validators:
//   - core/engine/Phonotactics.cpp           (wstring_view path / Path 2)
//   - core/engine/PhonotacticsValidator.cpp  (CharState path / Path 1, hot)
//
// This header anchors the T2.1 phonology consolidation work (see
// docs/TODO.md "Vietnamese-rule consolidation"). All entries are
// `constexpr` / `noexcept` — zero runtime cost, no allocation.
//
// Day-1 lifted IsFrontBaseVowel.
// Day-2 lifts VowelSlot/Key1-3 packed-key encoding + F_* coda bitmasks +
// kVCPairRules per-nucleus allowed-coda table + GetAllowedFinals lookup.

#pragma once

#include <cstddef>
#include <cstdint>

namespace NextKey {
namespace Phonology {

// =============================================================================
// Front-vowel classifier (Day-1)
// =============================================================================
// Front vowels for orthographic rules (c/k, g/gh, ng/ngh agreement).
// Modifier marks (ê = e+circumflex, etc.) do not change the front/back class:
// callers pass the *base* (post-Decompose) wchar.
[[nodiscard]] constexpr bool IsFrontBaseVowel(wchar_t base) noexcept {
    return base == L'e' || base == L'i' || base == L'y';
}

// =============================================================================
// Vowel nucleus packed-key encoding (Day-2 lift from PhonotacticsValidator)
// =============================================================================
// Each vowel slot packs (base_index << 2) | mod_ordinal into a uint8_t.
// 1-3 slots are then packed into a uint32_t with a top-byte length prefix
// (prevents Key1/Key2/Key3 collisions). Both validators encode their input
// into the same key space and share a single rule table below.

constexpr uint8_t kA = 0, kE = 1, kI = 2, kO = 3, kU = 4, kY = 5;
constexpr uint8_t kNone = 0, kCirc = 1, kBrev = 2, kHorn = 3;

// Sentinel returned by BaseIndex / VowelSlot for non-vowel input. Distinct
// from any valid (base << 2 | mod) packing — max legal slot is 5*4|3 = 0x17.
constexpr uint8_t kInvalidBaseIndex = 0xFF;

[[nodiscard]] constexpr uint8_t VowelSlot(uint8_t base, uint8_t mod) noexcept {
    return static_cast<uint8_t>((base << 2) | mod);
}

[[nodiscard]] constexpr uint8_t BaseIndex(wchar_t base) noexcept {
    switch (base) {
        case L'a': return kA;
        case L'e': return kE;
        case L'i': return kI;
        case L'o': return kO;
        case L'u': return kU;
        case L'y': return kY;
        default:   return kInvalidBaseIndex;
    }
}

[[nodiscard]] constexpr uint32_t Key1(uint8_t s0) noexcept {
    return (1u << 24) | static_cast<uint32_t>(s0);
}
[[nodiscard]] constexpr uint32_t Key2(uint8_t s0, uint8_t s1) noexcept {
    return (2u << 24) | (static_cast<uint32_t>(s0) << 8) | s1;
}
[[nodiscard]] constexpr uint32_t Key3(uint8_t s0, uint8_t s1, uint8_t s2) noexcept {
    return (3u << 24) | (static_cast<uint32_t>(s0) << 16) | (static_cast<uint32_t>(s1) << 8) | s2;
}

// =============================================================================
// Final-consonant bitmask + per-nucleus allowed-coda table (Day-2 lift)
// =============================================================================
// Bitmask encoding for coda consonants. Each phonotactic rule entry below
// lists which finals are allowed after a given vowel nucleus.
// Source: Vietnamese phonology + Unikey VCPairList reference.

constexpr uint16_t F_c  = 0x001;
constexpr uint16_t F_ch = 0x002;
constexpr uint16_t F_k  = 0x004;
constexpr uint16_t F_m  = 0x008;
constexpr uint16_t F_n  = 0x010;
constexpr uint16_t F_ng = 0x020;
constexpr uint16_t F_nh = 0x040;
constexpr uint16_t F_p  = 0x080;
constexpr uint16_t F_t  = 0x100;
constexpr uint16_t F_ALL       = F_c | F_ch | F_k | F_m | F_n | F_ng | F_nh | F_p | F_t;
constexpr uint16_t F_NO_CH_NH  = F_ALL & ~(F_ch | F_nh);  // c, k, m, n, ng, p, t

struct VCPairRule {
    uint32_t vowelKey;
    uint16_t allowedFinals;
};

constexpr VCPairRule kVCPairRules[] = {
    // === Single vowels ===
    { Key1(VowelSlot(kA, kNone)), F_ALL },                                      // a: all finals
    { Key1(VowelSlot(kA, kCirc)), F_NO_CH_NH },                                 // â: no ch, nh
    { Key1(VowelSlot(kA, kBrev)), F_NO_CH_NH },                                 // ă: no ch, nh
    { Key1(VowelSlot(kE, kNone)), F_ALL },                                      // e: all finals
    { Key1(VowelSlot(kE, kCirc)), F_c | F_ch | F_m | F_n | F_nh | F_p | F_t }, // ê: no ng
    { Key1(VowelSlot(kI, kNone)), F_ALL & ~F_ng },                               // i: all finals except ng
    { Key1(VowelSlot(kO, kNone)), F_NO_CH_NH },                                 // o: no ch, nh
    { Key1(VowelSlot(kO, kCirc)), F_NO_CH_NH },                                 // ô: no ch, nh
    { Key1(VowelSlot(kO, kHorn)), F_m | F_n | F_p | F_t },                     // ơ: only m, n, p, t
    { Key1(VowelSlot(kU, kNone)), F_NO_CH_NH },                                 // u: no ch, nh
    { Key1(VowelSlot(kU, kHorn)), F_NO_CH_NH },                                 // ư: no ch, nh
    { Key1(VowelSlot(kY, kNone)), F_t },                                        // y: only t

    // === Double vowels with coda ===
    { Key2(VowelSlot(kI, kNone), VowelSlot(kE, kCirc)),  F_c | F_m | F_n | F_ng | F_p | F_t },  // iê: no ch, nh
    { Key2(VowelSlot(kO, kNone), VowelSlot(kA, kNone)),  F_ALL },                                 // oa: all finals
    { Key2(VowelSlot(kO, kNone), VowelSlot(kA, kBrev)),  F_c | F_n | F_ng | F_t },               // oă: c, n, ng, t
    { Key2(VowelSlot(kO, kNone), VowelSlot(kE, kNone)),  F_m | F_n | F_ng | F_t },               // oe: m, n, ng, t
    { Key2(VowelSlot(kO, kNone), VowelSlot(kO, kNone)),  F_c | F_ng },                            // oo: only c, ng
    { Key2(VowelSlot(kU, kNone), VowelSlot(kA, kCirc)),  F_n | F_ng | F_t },                      // uâ: n, ng, t
    { Key2(VowelSlot(kU, kNone), VowelSlot(kE, kCirc)),  F_ch | F_n | F_nh },                     // uê: ch, n, nh
    { Key2(VowelSlot(kU, kNone), VowelSlot(kO, kCirc)),  F_c | F_m | F_n | F_ng | F_p | F_t },   // uô: no ch, nh
    { Key2(VowelSlot(kU, kNone), VowelSlot(kO, kHorn)),  F_c | F_m | F_n | F_ng | F_p | F_t },   // uơ: no ch, nh
    { Key2(VowelSlot(kU, kNone), VowelSlot(kY, kNone)),  F_ch | F_n | F_nh | F_t },              // uy: ch, n, nh, t
    { Key2(VowelSlot(kU, kHorn), VowelSlot(kO, kHorn)),  F_c | F_m | F_n | F_ng | F_p | F_t },   // ươ: no ch, nh
    { Key2(VowelSlot(kY, kNone), VowelSlot(kE, kCirc)),  F_c | F_m | F_n | F_ng | F_p | F_t },   // yê: same as iê

    // === Triple vowels with coda ===
    { Key3(VowelSlot(kU, kNone), VowelSlot(kY, kNone), VowelSlot(kE, kCirc)), F_n | F_t },       // uyê: n, t
};

constexpr size_t kVCPairRuleCount = sizeof(kVCPairRules) / sizeof(kVCPairRules[0]);

// Look up allowed finals for a vowel key. Returns 0 if the nucleus has no
// VCPair entry — caller decides whether to treat that as "no restriction"
// (Path 1 lenient default) or to require an entry (stricter validation).
[[nodiscard]] constexpr uint16_t GetAllowedFinals(uint32_t vowelKey) noexcept {
    for (size_t i = 0; i < kVCPairRuleCount; ++i) {
        if (kVCPairRules[i].vowelKey == vowelKey) return kVCPairRules[i].allowedFinals;
    }
    return 0;
}

}  // namespace Phonology
}  // namespace NextKey
