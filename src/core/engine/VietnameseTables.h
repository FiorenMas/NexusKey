// VKey - Shared Vietnamese Lookup Tables
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
// Dual-licensed: AGPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
//
// Flat constexpr arrays for O(1) composition lookups.
// Used by TypingEngine (Telex, VNI, and Combined input methods).

#pragma once

#include <cwchar>
#include <cwctype>
#include <iterator>

namespace NextKey {

//=============================================================================
// Modifier Lookup: base vowel + modifier → modified vowel
// Index: VowelBaseIndex(base) × 3 + ModifierIndex(mod)
// base: a=0, e=1, o=2, u=3
// mod:  Circumflex=0, Breve=1, Horn=2
//=============================================================================

constexpr wchar_t kModifiedVowel[4][3] = {
    /* a */ { L'\x00E2', L'\x0103', 0         },  // â, ă, (N/A)
    /* e */ { L'\x00EA', 0,         0         },  // ê
    /* o */ { L'\x00F4', 0,         L'\x01A1' },  // ô, (N/A), ơ
    /* u */ { 0,         0,         L'\x01B0' },  // (N/A), (N/A), ư
};

/// Map base vowel to index: a→0, e→1, o→2, u→3, else -1
constexpr int VowelBaseIndex(wchar_t base) {
    switch (base) {
        case L'a': return 0;
        case L'e': return 1;
        case L'o': return 2;
        case L'u': return 3;
        default:   return -1;
    }
}

//=============================================================================
// Tone Lookup: (possibly-modified) vowel + tone → toned vowel
// Index: ToneBaseIndex(ch) × 5 + ToneSlot(tone)
// tone: Acute=0, Grave=1, Hook=2, Tilde=3, Dot=4
//=============================================================================

// 12 base vowels × 5 tones
// Row order: a=0, â=1, ă=2, e=3, ê=4, i=5, o=6, ô=7, ơ=8, u=9, ư=10, y=11
constexpr wchar_t kTonedVowel[12][5] = {
    /* a  */ { L'\x00E1', L'\x00E0', L'\x1EA3', L'\x00E3', L'\x1EA1' },  // á à ả ã ạ
    /* â  */ { L'\x1EA5', L'\x1EA7', L'\x1EA9', L'\x1EAB', L'\x1EAD' },  // ấ ầ ẩ ẫ ậ
    /* ă  */ { L'\x1EAF', L'\x1EB1', L'\x1EB3', L'\x1EB5', L'\x1EB7' },  // ắ ằ ẳ ẵ ặ
    /* e  */ { L'\x00E9', L'\x00E8', L'\x1EBB', L'\x1EBD', L'\x1EB9' },  // é è ẻ ẽ ẹ
    /* ê  */ { L'\x1EBF', L'\x1EC1', L'\x1EC3', L'\x1EC5', L'\x1EC7' },  // ế ề ể ễ ệ
    /* i  */ { L'\x00ED', L'\x00EC', L'\x1EC9', L'\x0129', L'\x1ECB' },  // í ì ỉ ĩ ị
    /* o  */ { L'\x00F3', L'\x00F2', L'\x1ECF', L'\x00F5', L'\x1ECD' },  // ó ò ỏ õ ọ
    /* ô  */ { L'\x1ED1', L'\x1ED3', L'\x1ED5', L'\x1ED7', L'\x1ED9' },  // ố ồ ổ ỗ ộ
    /* ơ  */ { L'\x1EDB', L'\x1EDD', L'\x1EDF', L'\x1EE1', L'\x1EE3' },  // ớ ờ ở ỡ ợ
    /* u  */ { L'\x00FA', L'\x00F9', L'\x1EE7', L'\x0169', L'\x1EE5' },  // ú ù ủ ũ ụ
    /* ư  */ { L'\x1EE9', L'\x1EEB', L'\x1EED', L'\x1EEF', L'\x1EF1' },  // ứ ừ ử ữ ự
    /* y  */ { L'\x00FD', L'\x1EF3', L'\x1EF7', L'\x1EF9', L'\x1EF5' },  // ý ỳ ỷ ỹ ỵ
};

/// Map (possibly modified) vowel to tone table row index
constexpr int ToneBaseIndex(wchar_t ch) {
    switch (ch) {
        case L'a':         return 0;
        case L'\x00E2':    return 1;   // â
        case L'\x0103':    return 2;   // ă
        case L'e':         return 3;
        case L'\x00EA':    return 4;   // ê
        case L'i':         return 5;
        case L'o':         return 6;
        case L'\x00F4':    return 7;   // ô
        case L'\x01A1':    return 8;   // ơ
        case L'u':         return 9;
        case L'\x01B0':    return 10;  // ư
        case L'y':         return 11;
        default:           return -1;
    }
}

/// Strip tone from a Vietnamese vowel, keeping base + modifier intact.
/// 'ắ' → 'ă', 'ầ' → 'â', 'ớ' → 'ơ', 'ừ' → 'ư', 'á' → 'a', etc.
/// Non-Vietnamese or untoned chars pass through unchanged.
[[nodiscard]] inline wchar_t StripTone(wchar_t ch) noexcept {
    // Table of untoned vowels indexed by kTonedVowel row
    constexpr wchar_t kToneBase[] = {
        L'a', L'\x00E2', L'\x0103', L'e', L'\x00EA', L'i',
        L'o', L'\x00F4', L'\x01A1', L'u', L'\x01B0', L'y'
    };
    // Already untoned?
    if (ToneBaseIndex(ch) >= 0) return ch;
    // Search toned vowel table
    wchar_t low = towlower(ch);
    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 5; ++j) {
            if (kTonedVowel[i][j] == low) return kToneBase[i];
        }
    }
    return ch;
}

// Forward declaration — definition lives below after the case-conversion helpers.
[[nodiscard]] inline wchar_t ToLowerVietnamese(wchar_t ch) noexcept;

//=============================================================================
// Decompose a Vietnamese Unicode char into (base, modifier index, tone index)
// Used by SeedFromText to rebuild engine state from committed text.
//
// modIdx:  -1=None, 0=Circumflex, 1=Breve, 2=Horn, 3=Stroke (d only)
// toneIdx: -1=None, 0=Acute, 1=Grave, 2=Hook, 3=Tilde, 4=Dot
//
// Returns false for non-letter chars (digits, punctuation, whitespace).
// Base is always lowercase a-z; isUpper preserves original case.
//=============================================================================
[[nodiscard]] inline bool DecomposeVietChar(wchar_t ch, wchar_t& baseOut, int& modIdxOut,
                                             int& toneIdxOut, bool& isUpperOut) noexcept {
    wchar_t lower = ToLowerVietnamese(ch);
    isUpperOut = (lower != ch);
    modIdxOut = -1;
    toneIdxOut = -1;
    baseOut = 0;

    // Special: đ
    if (lower == L'\x0111') {
        baseOut = L'd';
        modIdxOut = 3;  // Stroke
        return true;
    }

    // Helper: decompose toned-base char (a, â, ă, e, ê, i, o, ô, ơ, u, ư, y) → base + mod
    auto decomposeToneBase = [&](wchar_t tb) -> bool {
        switch (tb) {
            case L'a': baseOut = L'a'; return true;
            case L'e': baseOut = L'e'; return true;
            case L'i': baseOut = L'i'; return true;
            case L'o': baseOut = L'o'; return true;
            case L'u': baseOut = L'u'; return true;
            case L'y': baseOut = L'y'; return true;
            case L'\x00E2': baseOut = L'a'; modIdxOut = 0; return true;  // â
            case L'\x0103': baseOut = L'a'; modIdxOut = 1; return true;  // ă
            case L'\x00EA': baseOut = L'e'; modIdxOut = 0; return true;  // ê
            case L'\x00F4': baseOut = L'o'; modIdxOut = 0; return true;  // ô
            case L'\x01A1': baseOut = L'o'; modIdxOut = 2; return true;  // ơ
            case L'\x01B0': baseOut = L'u'; modIdxOut = 2; return true;  // ư
            default: return false;
        }
    };

    // Toned vowel? Search kTonedVowel.
    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 5; ++j) {
            if (kTonedVowel[i][j] == lower) {
                constexpr wchar_t kToneBaseLocal[] = {
                    L'a', L'\x00E2', L'\x0103', L'e', L'\x00EA', L'i',
                    L'o', L'\x00F4', L'\x01A1', L'u', L'\x01B0', L'y'
                };
                toneIdxOut = j;
                return decomposeToneBase(kToneBaseLocal[i]);
            }
        }
    }

    // Untoned modified vowel or plain vowel
    if (decomposeToneBase(lower)) return true;

    // Plain latin consonant
    if (lower >= L'a' && lower <= L'z') {
        baseOut = lower;
        return true;
    }

    return false;
}

//=============================================================================
// Diphthong Tone Placement — shared between Telex and VNI engines
// Table[first_vowel][second_vowel] → 0=no rule, 1=FIRST, 2=SECOND
// Vowel index: a=0, e=1, i=2, o=3, u=4, y=5
//=============================================================================

/// Map base vowel to diphthong table index
constexpr int DiphthongVowelIndex(wchar_t base) {
    switch (base) {
        case L'a': return 0;
        case L'e': return 1;
        case L'i': return 2;
        case L'o': return 3;
        case L'u': return 4;
        case L'y': return 5;
        default:   return -1;
    }
}

/// Classic placement: oa, oe → coda-aware (hòa without coda, hoàn with coda)
/// Rule: 0=none, 1=FIRST, 2=SECOND, 3=FIRST-without-coda/SECOND-with-coda
constexpr uint8_t kDiphthongClassic[6][6] = {
    //          a  e  i  o  u  y
    /* a */ {   0, 0, 1, 1, 1, 1 },  // ai, ao, au, ay
    /* e */ {   0, 0, 1, 1, 1, 0 },  // ei, eo, eu
    /* i */ {   1, 0, 0, 0, 1, 0 },  // ia, iu (ii=0: not a diphthong)
    /* o */ {   3, 3, 1, 0, 1, 0 },  // oa=CODA_AWARE, oe=CODA_AWARE, oi, ou
    /* u */ {   1, 1, 1, 0, 0, 3 },  // ua, ue, ui, uy=CODA_AWARE (classic: úy no coda, huỳnh with coda) (uu=0)
    /* y */ {   0, 0, 0, 0, 3, 0 },  // yu=CODA_AWARE: yủn(coda→u), khuỷu(no coda→y)
};

/// Modern placement: oa, oe, ue → tone on SECOND (new-style: hóa, xoé, thuế)
constexpr uint8_t kDiphthongModern[6][6] = {
    //          a  e  i  o  u  y
    /* a */ {   0, 0, 1, 1, 1, 1 },  // (same as classic)
    /* e */ {   0, 0, 1, 1, 1, 0 },  // (same as classic)
    /* i */ {   1, 0, 0, 0, 1, 0 },  // (same as classic; ii=0)
    /* o */ {   2, 2, 1, 0, 1, 0 },  // oa=SECOND, oe=SECOND (modern)
    /* u */ {   1, 2, 1, 2, 0, 2 },  // ue=SECOND, uo=SECOND, uy=SECOND (uu=0)
    /* y */ {   0, 0, 0, 0, 3, 0 },  // yu=CODA_AWARE: yủn(coda→u), khuỷu(no coda→y)
};

/// Check if three vowel bases form a triphthong (Modern only): tone on MIDDLE vowel
[[nodiscard]] constexpr bool IsTriphthong(wchar_t v1, wchar_t v2, wchar_t v3) noexcept {
    // Flat comparison is faster than loop, no branch misprediction
    return (v1 == L'o' && v2 == L'a' && v3 == L'i') ||  // oai
           (v1 == L'o' && v2 == L'e' && v3 == L'o') ||  // oeo
           (v1 == L'u' && v2 == L'y' && v3 == L'a') ||  // uya
           (v1 == L'u' && v2 == L'y' && v3 == L'u') ||  // uyu
           (v1 == L'o' && v2 == L'a' && v3 == L'o') ||  // oao
           (v1 == L'o' && v2 == L'a' && v3 == L'y');    // oay
}

/// Smart-accent intermediate triphthong: 3-vowel cluster that is the in-progress
/// form of a Vietnamese triphthong where the LAST vowel needs a circumflex to
/// complete the syllable (e.g. uye → uyê: chuyện/tuyết/nguyễn/xuyến). When the
/// nucleus matches AND no vowel has a modifier yet, tone placement should fall
/// on the trailing vowel (the one awaiting circumflex) so that a later
/// free-marked `e`/`o` promotes base→modified-base and the tone stays put via
/// P2 (modified-vowel priority). Mirrors the existing i+e and y+e exceptions
/// in EnglishProtection.h::HasInvalidAdjacentVowelPair.
[[nodiscard]] constexpr bool IsBareSmartTriphthongTail(
        wchar_t v1, wchar_t v2, wchar_t v3) noexcept {
    return (v1 == L'u' && v2 == L'y' && v3 == L'e');  // uye → uyê
    // Extend here when more Vietnamese triphthong intermediates surface.
}

//=============================================================================
// Vietnamese-aware uppercase conversion (shared)
//=============================================================================

[[nodiscard]] inline wchar_t ToUpperVietnamese(wchar_t ch) noexcept {
    // Latin-1 Supplement
    if (ch >= 0x00E0 && ch <= 0x00F6) return ch - 0x20;
    if (ch >= 0x00F8 && ch <= 0x00FE) return ch - 0x20;

    // Latin Extended-A
    if (ch == L'\x0103') return L'\x0102';  // ă → Ă
    if (ch == L'\x0111') return L'\x0110';  // đ → Đ
    if (ch == L'\x0129') return L'\x0128';  // ĩ → Ĩ  (towupper() fails for these on Windows)
    if (ch == L'\x0169') return L'\x0168';  // ũ → Ũ

    // Latin Extended-B
    if (ch == L'\x01A1') return L'\x01A0';  // ơ → Ơ
    if (ch == L'\x01B0') return L'\x01AF';  // ư → Ư

    // Vietnamese toned vowels (Latin Extended Additional: 0x1EA0–0x1EF9)
    if (ch >= 0x1EA0 && ch <= 0x1EF9) {
        if (ch & 1) return ch - 1;  // Odd (lowercase) → Even (uppercase)
        return ch;
    }

    return towupper(ch);
}

//=============================================================================
// Vietnamese-aware lowercase conversion (shared)
//=============================================================================

[[nodiscard]] inline wchar_t ToLowerVietnamese(wchar_t ch) noexcept {
    // Latin-1 Supplement
    if (ch >= 0x00C0 && ch <= 0x00D6) return ch + 0x20;
    if (ch >= 0x00D8 && ch <= 0x00DE) return ch + 0x20;

    // Latin Extended-A
    if (ch == L'\x0102') return L'\x0103';  // Ă → ă
    if (ch == L'\x0110') return L'\x0111';  // Đ → đ

    // Latin Extended-B
    if (ch == L'\x01A0') return L'\x01A1';  // Ơ → ơ
    if (ch == L'\x01AF') return L'\x01B0';  // Ư → ư

    // Vietnamese toned vowels (Latin Extended Additional: 0x1EA0–0x1EF9)
    if (ch >= 0x1EA0 && ch <= 0x1EF9) {
        if (!(ch & 1)) return ch + 1;  // Even (uppercase) → Odd (lowercase)
    }

    return towlower(ch);
}

//=============================================================================
// Shared character classification — no towlower() call.
//=============================================================================

/// Vowel check handling both cases directly (no locale-dependent towlower)
constexpr bool IsVowelChar(wchar_t c) noexcept {
    return c == L'a' || c == L'e' || c == L'i' || c == L'o' || c == L'u' || c == L'y' ||
           c == L'A' || c == L'E' || c == L'I' || c == L'O' || c == L'U' || c == L'Y';
}

}  // namespace NextKey
