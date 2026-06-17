// VKey - Vietnamese Phonotactics Implementation
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial

#include "Phonotactics.h"

#include <array>
#include <cstddef>
#include <string_view>

#include "DefaultPhonologyRules.h"
#include "VietnamesePhonologyData.h"
#include "VietnameseTables.h"

namespace NextKey {
namespace Phonology {
namespace {

// =============================================================================
// Vowel decomposition: rendered Vietnamese wchar_t → (base, modifier kind).
// `base` is one of L'a', L'e', L'i', L'o', L'u', L'y'. Other chars return base=0.
// =============================================================================

enum class VowelMod : uint8_t {
    None,
    Circumflex,  // â ê ô
    Breve,       // ă
    Horn         // ơ ư
};

struct VowelInfo {
    wchar_t base;
    VowelMod mod;
};

[[nodiscard]] VowelInfo Decompose(wchar_t c) noexcept {
    switch (c) {
        case L'a': return {L'a', VowelMod::None};
        case L'\x0103': return {L'a', VowelMod::Breve};        // ă
        case L'\x00E2': return {L'a', VowelMod::Circumflex};   // â
        case L'e': return {L'e', VowelMod::None};
        case L'\x00EA': return {L'e', VowelMod::Circumflex};   // ê
        case L'i': return {L'i', VowelMod::None};
        case L'o': return {L'o', VowelMod::None};
        case L'\x00F4': return {L'o', VowelMod::Circumflex};   // ô
        case L'\x01A1': return {L'o', VowelMod::Horn};         // ơ
        case L'u': return {L'u', VowelMod::None};
        case L'\x01B0': return {L'u', VowelMod::Horn};         // ư
        case L'y': return {L'y', VowelMod::None};
        default:   return {0, VowelMod::None};
    }
}

// Tone-placement tables and triphthong predicate are shared with TypingEngine
// via VietnameseTables.h — see NextKey::kDiphthongClassic, NextKey::kDiphthongModern,
// NextKey::DiphthongVowelIndex, NextKey::IsTriphthong.

// =============================================================================
// Closed vowel sequences — must NOT have a coda (28 entries from RuleTiengViet).
// Comparing rendered Vietnamese strings directly for clarity.
// =============================================================================
constexpr std::wstring_view kClosedVowels[] = {
    L"ai", L"ao", L"au", L"ay",
    L"\x00E2u",                 // âu
    L"\x00E2y",                 // ây
    L"eo",
    L"\x00EAu",                 // êu
    L"ia", L"iu", L"oi",
    L"\x00F4i",                 // ôi
    L"\x01A1i",                 // ơi
    L"ui",
    L"\x01B0a",                 // ưa
    L"\x01B0i",                 // ưi
    L"\x01B0u",                 // ưu
    L"i\x00EAu",                // iêu
    L"u\x00F4i",                // uôi
    L"uyu",
    L"\x01B0\x01A1i",           // ươi
    L"\x01B0\x01A1u",           // ươu
    L"oai", L"oay",
    L"u\x00E2y",                // uây
    L"uya", L"oeo", L"oao",
};

// =============================================================================
// Pending vowel sequences — REQUIRE a coda (10 entries from RuleTiengViet).
// =============================================================================
constexpr std::wstring_view kPendingVowels[] = {
    L"\x0103",                  // ă
    L"\x00E2",                  // â
    L"i\x00EA",                 // iê
    L"o\x0103",                 // oă
    L"u\x00E2",                 // uâ
    L"u\x00F4",                 // uô
    L"oo", L"\x00F4\x00F4",     // oo, ôô
    L"\x01B0\x01A1",            // ươ
    L"uy\x00EA",                // uyê
};

[[nodiscard]] bool IsClosedVowelSeq(std::wstring_view vowelSeq) noexcept {
    for (auto closedSeq : kClosedVowels) {
        if (closedSeq == vowelSeq) return true;
    }
    return false;
}

[[nodiscard]] bool IsPendingVowelSeq(std::wstring_view vowelSeq) noexcept {
    for (auto pendingSeq : kPendingVowels) {
        if (pendingSeq == vowelSeq) return true;
    }
    return false;
}

// =============================================================================
// Stop-final coda (c, ch, p, t) → tone restricted to Acute (sắc) or Dot (nặng).
// =============================================================================
[[nodiscard]] constexpr bool IsStopFinalCoda(std::wstring_view coda) noexcept {
    return coda == L"c" || coda == L"ch" || coda == L"p" || coda == L"t";
}

[[nodiscard]] constexpr bool ToneAllowedForCoda(std::wstring_view coda, Tone tone) noexcept {
    if (!IsStopFinalCoda(coda)) return true;
    return tone == Tone::None || tone == Tone::Acute || tone == Tone::Dot;
}

// =============================================================================
// Onset / coda lexicon for CanComplete parsing.
// =============================================================================
constexpr std::wstring_view kValidOnsets[] = {
    L"",                              // vowel-initial syllable
    L"b", L"c", L"d", L"\x0111",      // d, đ
    L"g", L"h", L"k", L"l", L"m", L"n",
    L"p", L"q", L"r", L"s", L"t", L"v", L"x",
    L"ch", L"gh", L"gi", L"kh", L"ng", L"nh", L"ph", L"qu", L"th", L"tr",
    L"ngh",
};

[[nodiscard]] bool IsKnownOnset(std::wstring_view text) noexcept {
    for (auto onset : kValidOnsets) {
        if (onset == text) return true;
    }
    return false;
}

// True if `text` is a valid lowercase ASCII onset prefix (incl. mid-typing like
// "n" before completing to "ng" or "nh"). Used by CanComplete's no-vowel branch.
[[nodiscard]] bool IsKnownOnsetPrefix(std::wstring_view text) noexcept {
    if (text.empty()) return true;
    // Any single ASCII consonant that can begin a valid onset.
    if (text.size() == 1) {
        wchar_t leadChar = text[0];
        // Reject pure non-letters / vowels.
        if (Decompose(leadChar).base != 0) return false;
        return (leadChar >= L'a' && leadChar <= L'z') || leadChar == L'\x0111';  // đ
    }
    // For 2+ chars, must match a known onset exactly. The only 3-char onset
    // is "ngh"; its 2-char prefix "ng" is itself a known onset, so no separate
    // prefix branch is needed.
    return IsKnownOnset(text);
}

constexpr std::wstring_view kValidCodas[] = {
    L"c", L"m", L"n", L"p", L"t",
    L"ch", L"ng", L"nh",
};

[[nodiscard]] bool IsKnownCoda(std::wstring_view text) noexcept {
    for (auto coda : kValidCodas) {
        if (coda == text) return true;
    }
    return false;
}

// Per-nucleus allowed-coda compatibility, sourced from the canonical VCPair
// bitmask in VietnamesePhonologyData.h (T2.1 Day-2). Encodes the rendered
// vowelSeq + coda into the same packed-key + F_* bitmask space the CharState
// path uses, then performs one bitmask lookup. Replaces the coarser N1/N2/N3
// approximation that lived here in T3.

// Pack a rendered nucleus (1-3 vowels) into the same packed key the CharState
// path uses. Returns 0 when the nucleus is unencodable (no vowels, more than
// 3 vowels, or a non-recognised char) — caller treats 0 as "no VCPair entry
// → lenient pass-through".
[[nodiscard]] constexpr uint32_t WstringViewToVowelKey(std::wstring_view vowelSeq) noexcept {
    uint8_t slots[3] = { 0, 0, 0 };
    size_t count = 0;
    for (wchar_t ch : vowelSeq) {
        VowelInfo info = Decompose(ch);
        if (info.base == 0) return 0;          // unrecognised char in nucleus
        if (count >= 3) return 0;              // too many vowels for VCPair
        uint8_t baseIdx = NextKey::Phonology::BaseIndex(info.base);
        if (baseIdx == NextKey::Phonology::kInvalidBaseIndex) return 0;
        slots[count++] = NextKey::Phonology::VowelSlot(baseIdx,
                                                        static_cast<uint8_t>(info.mod));
    }
    switch (count) {
        case 1:  return NextKey::Phonology::Key1(slots[0]);
        case 2:  return NextKey::Phonology::Key2(slots[0], slots[1]);
        case 3:  return NextKey::Phonology::Key3(slots[0], slots[1], slots[2]);
        default: return 0;
    }
}

// Map a rendered coda string to the F_* bitmask. Returns 0 for unknown
// codas (caller treats as lenient pass-through).
[[nodiscard]] constexpr uint16_t CodaToFinalBit(std::wstring_view coda) noexcept {
    using namespace NextKey::Phonology;
    if (coda.size() == 1) {
        switch (coda[0]) {
            case L'c': return F_c;
            case L'k': return F_k;
            case L'm': return F_m;
            case L'n': return F_n;
            case L'p': return F_p;
            case L't': return F_t;
            default:   return 0;
        }
    }
    if (coda.size() == 2) {
        if (coda == L"ch") return F_ch;
        if (coda == L"ng") return F_ng;
        if (coda == L"nh") return F_nh;
    }
    return 0;
}

// Lenient by default: if any of the inputs (nucleus, coda) cannot be encoded
// or has no entry in the VCPair table, accept the syllable. The strictness
// applies only when both sides resolve to known, table-listed values.
// Rule data is resolved through the injected IPhonologyRules pack so future
// dialectal variants (RulePackId factory) plug in without forking this code.
[[nodiscard]] bool IsCodaValidForNucleus(const IPhonologyRules& rules,
                                          std::wstring_view vowelSeq,
                                          std::wstring_view coda) noexcept {
    if (coda.empty()) return true;
    uint32_t vowelKey = WstringViewToVowelKey(vowelSeq);
    if (vowelKey == 0) return true;
    uint16_t allowed = rules.AllowedFinalsForVowelKey(vowelKey);
    if (allowed == 0) return true;
    uint16_t bit = CodaToFinalBit(coda);
    if (bit == 0) return true;
    return (allowed & bit) != 0;
}

// Vietnamese orthography splits c/k, g/gh, ng/ngh by vowel frontness.
// Front-vowel classifier lives in VietnamesePhonologyData.h, shared with the
// CharState path in PhonotacticsValidator.cpp (T2.1 consolidation Day-1).

[[nodiscard]] bool IsOnsetVowelAgreementValid(
        const IPhonologyRules& rules,
        std::wstring_view onset,
        std::wstring_view vowelSeq) noexcept {
    // qu intentionally exempted: book lists oa/oă/oe/uy/uơ/uô/uê/uâ as the
    // canonical labial diphthongs after qu, but qua/quan/quát/quanh sit
    // outside that list and are everyday words.
    if (onset == L"qu") return true;

    const bool needsAgreement =
        onset == L"c" || onset == L"k"  ||
        onset == L"g" || onset == L"gh" ||
        onset == L"ng" || onset == L"ngh";
    if (!needsAgreement) return true;

    wchar_t firstBase = 0;
    for (wchar_t ch : vowelSeq) {
        VowelInfo info = Decompose(ch);
        if (info.base != 0) { firstBase = info.base; break; }
    }
    if (firstBase == 0) return true;

    const bool wantsFront = (onset == L"k" || onset == L"gh" || onset == L"ngh");
    return wantsFront == rules.IsFrontBaseVowel(firstBase);
}

// =============================================================================
// Core priority logic for tone placement: P1 horn > P2 modified > P3 diphthong /
// triphthong > P4 rightmost. Operates on a rendered wstring_view of the vowel
// nucleus (Decompose maps each char to base + modifier).
// =============================================================================
[[nodiscard]] size_t ComputeTonePosition(
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        bool modernOrtho) noexcept {
    // Real Vietnamese vowel nuclei are at most 3 chars, but the engine may pass
    // longer sequences for typo cases ("máaaaaaaa" — 9+ repeated vowels). Cap
    // generously to keep all such inputs in scope: P1/P2 must scan the full
    // sequence to find any horn / modifier regardless of where the user typed
    // it, and the rightmost-fallback (P4) must point at the actual last vowel.
    std::array<VowelInfo, 16> vowels{};
    size_t count = 0;
    for (wchar_t ch : vowelSeq) {
        if (count >= vowels.size()) break;
        VowelInfo info = Decompose(ch);
        if (info.base == 0) continue;  // skip non-vowels defensively
        vowels[count++] = info;
    }
    if (count == 0) return SIZE_MAX;

    // P1: last horn vowel wins (covers ươ → ơ).
    for (size_t i = count; i-- > 0; ) {
        if (vowels[i].mod == VowelMod::Horn) return i;
    }

    // P2: first non-horn modified vowel (â, ê, ô, ă).
    for (size_t i = 0; i < count; ++i) {
        if (vowels[i].mod == VowelMod::Circumflex || vowels[i].mod == VowelMod::Breve) {
            return i;
        }
    }

    // P3: diphthong / triphthong rules (need ≥ 2 vowels).
    if (count >= 2) {
        // Modern triphthong: tone on MIDDLE.
        if (modernOrtho && count >= 3) {
            if (NextKey::IsTriphthong(vowels[count - 3].base,
                                       vowels[count - 2].base,
                                       vowels[count - 1].base)) {
                return count - 2;
            }
        }

        // Smart-accent intermediate triphthong (uye → uyê: chuyện/tuyết/…).
        // See VietnameseTables.h::IsBareSmartTriphthongTail for rationale.
        // Ortho-independent: classic and modern both render chuyện identically.
        if (count == 3 &&
            vowels[0].mod == VowelMod::None &&
            vowels[1].mod == VowelMod::None &&
            vowels[2].mod == VowelMod::None &&
            NextKey::IsBareSmartTriphthongTail(vowels[0].base, vowels[1].base, vowels[2].base)) {
            return count - 1;
        }

        // Default pair: last two vowels.
        size_t firstPos = count - 2;
        size_t lastPos  = count - 1;
        int firstDiphIndex = NextKey::DiphthongVowelIndex(vowels[firstPos].base);
        int lastDiphIndex  = NextKey::DiphthongVowelIndex(vowels[lastPos].base);
        bool shifted = false;

        // Shift onto first two of a 3-vowel cluster when those have a diphthong
        // rule. Handles typo cases like "gaoi" (gạo + extra i) and classic "oai".
        if (count >= 3) {
            int shiftFirstIndex = NextKey::DiphthongVowelIndex(vowels[count - 3].base);
            if (shiftFirstIndex >= 0 && firstDiphIndex >= 0) {
                uint8_t shiftRule = modernOrtho
                    ? NextKey::kDiphthongModern[shiftFirstIndex][firstDiphIndex]
                    : NextKey::kDiphthongClassic[shiftFirstIndex][firstDiphIndex];
                if (shiftRule != 0) {
                    lastDiphIndex = firstDiphIndex;
                    firstDiphIndex = shiftFirstIndex;
                    firstPos = count - 3;
                    lastPos  = count - 2;
                    shifted = true;
                }
            }
        }

        if (firstDiphIndex >= 0 && lastDiphIndex >= 0) {
            uint8_t rule = modernOrtho
                ? NextKey::kDiphthongModern[firstDiphIndex][lastDiphIndex]
                : NextKey::kDiphthongClassic[firstDiphIndex][lastDiphIndex];
            if (rule == 3) {
                // Shifted with vowel-repeat at end → tone stays on first vowel
                // (typo "hoaa" / "oaa": don't slide tone onto the duplicated 'a').
                if (shifted && vowels[count - 1].base == vowels[count - 2].base) {
                    rule = 1;
                } else {
                    bool hasRemainder = (lastPos + 1 < count) || !coda.empty();
                    rule = hasRemainder ? 2 : 1;
                }
            }
            if (rule == 1) return firstPos;
            if (rule == 2) return lastPos;
        }
    }

    // P4: rightmost vowel.
    return count - 1;
}

// =============================================================================
// Simple parser for CanComplete: split partial → (onset, vowels, coda, leftover).
// Returns false if no plausible split exists.
// =============================================================================

struct ParsedSyllable {
    std::wstring_view onset;
    std::wstring_view vowels;
    std::wstring_view coda;
    std::wstring_view leftover;  // anything past the coda
    bool hasVowel = false;
};

[[nodiscard]] ParsedSyllable Parse(std::wstring_view text) noexcept {
    ParsedSyllable result;
    // Find first vowel.
    size_t firstVowelIdx = std::wstring_view::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        if (Decompose(text[i]).base != 0) { firstVowelIdx = i; break; }
    }
    if (firstVowelIdx == std::wstring_view::npos) {
        result.onset = text;
        return result;
    }
    result.onset = text.substr(0, firstVowelIdx);
    result.hasVowel = true;

    // Collect contiguous vowels.
    size_t lastVowelIdx = firstVowelIdx;
    for (size_t i = firstVowelIdx; i < text.size(); ++i) {
        if (Decompose(text[i]).base == 0) break;
        lastVowelIdx = i;
    }
    result.vowels = text.substr(firstVowelIdx, lastVowelIdx - firstVowelIdx + 1);

    // Coda is up to 2 consonants after vowels.
    std::wstring_view rest = text.substr(lastVowelIdx + 1);
    size_t codaLen = 0;
    while (codaLen < rest.size() && codaLen < 2 &&
           Decompose(rest[codaLen]).base == 0) {
        ++codaLen;
    }
    result.coda = rest.substr(0, codaLen);
    result.leftover = rest.substr(codaLen);
    return result;
}

}  // anonymous namespace

// =============================================================================
// IPhonotactics implementation
// =============================================================================

Phonotactics::Phonotactics() noexcept
    : rules_(DefaultPhonologyRules::Default()) {}

Phonotactics::Phonotactics(const IPhonologyRules& rules) noexcept
    : rules_(rules) {}

const Phonotactics& Phonotactics::Default() noexcept {
    static const Phonotactics instance;
    return instance;
}

size_t Phonotactics::TonePosition(
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        bool modernOrtho) const noexcept {
    return ComputeTonePosition(vowelSeq, coda, modernOrtho);
}

bool Phonotactics::IsValidSyllable(
        std::wstring_view onset,
        std::wstring_view vowelSeq,
        std::wstring_view coda,
        Tone tone,
        bool /*modernOrtho*/) const noexcept {
    if (vowelSeq.empty()) return false;

    // Onset / vowel front-back agreement (c/k, g/gh, ng/ngh). qu exempted.
    if (!IsOnsetVowelAgreementValid(rules_, onset, vowelSeq)) return false;

    // Closed vowels must NOT have a coda.
    if (!coda.empty() && IsClosedVowelSeq(vowelSeq)) return false;

    // Pending vowels MUST have a coda.
    if (coda.empty() && IsPendingVowelSeq(vowelSeq)) return false;

    // VCPair vowel-coda compatibility (per-nucleus allowed-coda bitmask,
    // shared with PhonotacticsValidator on the CharState path).
    if (!IsCodaValidForNucleus(rules_, vowelSeq, coda)) return false;

    // Stop-final coda restricts tone to Acute or Dot.
    if (!ToneAllowedForCoda(coda, tone)) return false;

    return true;
}

bool Phonotactics::CanComplete(std::wstring_view partial) const noexcept {
    if (partial.empty()) return true;

    ParsedSyllable parsed = Parse(partial);

    // No vowel at all — must be a valid onset prefix.
    if (!parsed.hasVowel) {
        return IsKnownOnsetPrefix(parsed.onset);
    }

    // Onset must be a known cluster (or empty for vowel-initial).
    if (!IsKnownOnset(parsed.onset)) return false;

    // Anything past the coda is a leftover keystroke past a closed syllable.
    if (!parsed.leftover.empty()) return false;

    // Coda (if any) must be a known coda. Single-char prefixes of multi-char
    // codas (c → ch, n → ng/nh) are themselves already valid codas, so any
    // non-known multi-char value is a hard fail.
    if (!parsed.coda.empty() && !IsKnownCoda(parsed.coda)) {
        return false;
    }

    return true;
}

}  // namespace Phonology
}  // namespace NextKey
