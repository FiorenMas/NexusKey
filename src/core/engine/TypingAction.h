// VKey - User-mappable input actions
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
// Dual-licensed: AGPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
//
// Action vocabulary for TypingEngine dispatch and (future) per-user keymap.
// Header is intentionally dependency-free so TypingConfig can hold a
// `customKeyMap: array<TypingAction, 128>` without circular includes.

#pragma once

#include <cstdint>
#include <string_view>

namespace NextKey {

/// Action that a user keystroke is classified as before dispatch.
/// `ClassifyKey` returns one of these based on (key, mode) only —
/// runtime applicability (state shape, escape flags, English protection,
/// spell-check gating) stays in the per-action handlers in TypingEngine.
enum class TypingAction : uint8_t {
    None = 0,        // Not an IME-bound key — handled as literal char

    // -- Tone application --
    ClearTone,       // Telex z, VNI 0
    ToneAcute,       // sắc — Telex s, VNI 1
    ToneGrave,       // huyền — Telex f, VNI 2
    ToneHook,        // hỏi — Telex r, VNI 3
    ToneTilde,       // ngã — Telex x, VNI 4
    ToneDot,         // nặng — Telex j, VNI 5

    // -- Telex modifier keys (per-vowel doubling triggers) --
    CircumflexA,     // Telex a (aa→â)
    CircumflexE,     // Telex e (ee→ê)
    CircumflexO,     // Telex o (oo→ô)
    HornW,           // Telex w (u→ư, o→ơ; ă fallback when no horn target)
    HornInsertO,     // Telex [ (always inserts ơ as new state)
    HornInsertU,     // Telex ] (always inserts ư as new state)
    StrokeD,         // Telex d (dd→đ)

    // -- VNI modifier keys (single-key, picks target vowel from state) --
    VniCircumflex,   // VNI 6
    VniHorn,         // VNI 7
    VniBreve,        // VNI 8
    VniStroke,       // VNI 9

    // -- Unikey-compatible actions --
    HornOrInsertU,           // Móc a,u,o → ă,ư,ơ; fallback: chèn ư nếu không match
    HornOrInsertUNoStart,    // Giống trên, nhưng KHÔNG chèn ư ở đầu từ
    UndoAllMarks,            // Thoát bỏ dấu (xoá tất cả modifier + tone)

    // -- Direct char insertion --
    InsertABreve,            // Chữ ă
    InsertABreveUpper,       // Chữ Ă
    InsertACircumflex,       // Chữ â
    InsertACircumflexUpper,  // Chữ Â
    InsertDStroke,           // Chữ đ
    InsertDStrokeUpper,      // Chữ Đ
    InsertECircumflex,       // Chữ ê
    InsertECircumflexUpper,  // Chữ Ê
    InsertOCircumflex,       // Chữ ô
    InsertOCircumflexUpper,  // Chữ Ô
    InsertOHorn,             // Chữ ơ
    InsertOHornUpper,        // Chữ Ơ
    InsertUHorn,             // Chữ ư
    InsertUHornUpper,        // Chữ Ư
};

[[nodiscard]] constexpr bool IsToneAction(TypingAction action) noexcept {
    return action >= TypingAction::ClearTone && action <= TypingAction::ToneDot;
}

[[nodiscard]] constexpr bool IsTelexModifierAction(TypingAction action) noexcept {
    return action >= TypingAction::CircumflexA && action <= TypingAction::StrokeD;
}

[[nodiscard]] constexpr bool IsVniModifierAction(TypingAction action) noexcept {
    return action >= TypingAction::VniCircumflex && action <= TypingAction::VniStroke;
}

[[nodiscard]] constexpr bool IsUserDefinedOnlyAction(TypingAction action) noexcept {
    return action >= TypingAction::HornOrInsertU && action <= TypingAction::InsertUHornUpper;
}

/// Insert-type actions can fire on an empty engine buffer (they synthesise a
/// fresh state). Tone/modifier actions need an existing vowel target, so the
/// HookEngine step 6d gate keeps `engine_->Count() > 0` for those. Used by
/// HookEngine to decide whether a customKeyMap-bound OEM punct key should be
/// routed into the engine even at word start. Excludes `HornOrInsertUNoStart`
/// by design — that variant explicitly suppresses word-start insertion.
[[nodiscard]] constexpr bool IsInsertTypeAction(TypingAction action) noexcept {
    if (action == TypingAction::HornInsertO || action == TypingAction::HornInsertU) return true;
    if (action == TypingAction::HornOrInsertU) return true;
    if (action == TypingAction::HornW) return true;  // Telex P8 inserts ư at empty
    if (action >= TypingAction::InsertABreve && action <= TypingAction::InsertUHornUpper) return true;
    return false;
}

/// Classify a key into a TypingAction based purely on (key, mode).
/// Pure function — no state lookup, no side effects. Returns
/// `TypingAction::None` for keys with no IME meaning under the
/// supplied mode (the dispatcher then handles them as literal chars).
///
/// `lower` must be `towlower(c)` of the original key. Pass mode flags
/// directly so this header stays free of TypingConfig.
///
/// Combined mode: pass both `isTelex=true` and `isVni=true`. Telex
/// classification wins for letters (s/f/r/x/j/a/e/o/w/d/z and
/// brackets); VNI classification wins for digits 0-9. There is no
/// overlap so the order is deterministic.
[[nodiscard]] constexpr TypingAction ClassifyKey(wchar_t lower,
                                                  bool isTelex,
                                                  bool isVni) noexcept {
    if (isTelex) {
        switch (lower) {
            case L'z': return TypingAction::ClearTone;
            case L's': return TypingAction::ToneAcute;
            case L'f': return TypingAction::ToneGrave;
            case L'r': return TypingAction::ToneHook;
            case L'x': return TypingAction::ToneTilde;
            case L'j': return TypingAction::ToneDot;
            case L'a': return TypingAction::CircumflexA;
            case L'e': return TypingAction::CircumflexE;
            case L'o': return TypingAction::CircumflexO;
            case L'w': return TypingAction::HornW;
            case L'[': return TypingAction::HornInsertO;
            case L']': return TypingAction::HornInsertU;
            case L'd': return TypingAction::StrokeD;
            default: break;
        }
    }
    if (isVni) {
        switch (lower) {
            case L'0': return TypingAction::ClearTone;
            case L'1': return TypingAction::ToneAcute;
            case L'2': return TypingAction::ToneGrave;
            case L'3': return TypingAction::ToneHook;
            case L'4': return TypingAction::ToneTilde;
            case L'5': return TypingAction::ToneDot;
            case L'6': return TypingAction::VniCircumflex;
            case L'7': return TypingAction::VniHorn;
            case L'8': return TypingAction::VniBreve;
            case L'9': return TypingAction::VniStroke;
            default: break;
        }
    }
    return TypingAction::None;
}

[[nodiscard]] inline std::string_view TypingActionToString(TypingAction action) noexcept {
    switch (action) {
        case TypingAction::None: return "None";
        case TypingAction::ClearTone: return "ClearTone";
        case TypingAction::ToneAcute: return "ToneAcute";
        case TypingAction::ToneGrave: return "ToneGrave";
        case TypingAction::ToneHook: return "ToneHook";
        case TypingAction::ToneTilde: return "ToneTilde";
        case TypingAction::ToneDot: return "ToneDot";
        case TypingAction::CircumflexA: return "CircumflexA";
        case TypingAction::CircumflexE: return "CircumflexE";
        case TypingAction::CircumflexO: return "CircumflexO";
        case TypingAction::HornW: return "HornW";
        case TypingAction::HornInsertO: return "HornInsertO";
        case TypingAction::HornInsertU: return "HornInsertU";
        case TypingAction::StrokeD: return "StrokeD";
        case TypingAction::VniCircumflex: return "VniCircumflex";
        case TypingAction::VniHorn: return "VniHorn";
        case TypingAction::VniBreve: return "VniBreve";
        case TypingAction::VniStroke: return "VniStroke";
        case TypingAction::HornOrInsertU: return "HornOrInsertU";
        case TypingAction::HornOrInsertUNoStart: return "HornOrInsertUNoStart";
        case TypingAction::UndoAllMarks: return "UndoAllMarks";
        case TypingAction::InsertABreve: return "InsertABreve";
        case TypingAction::InsertABreveUpper: return "InsertABreveUpper";
        case TypingAction::InsertACircumflex: return "InsertACircumflex";
        case TypingAction::InsertACircumflexUpper: return "InsertACircumflexUpper";
        case TypingAction::InsertDStroke: return "InsertDStroke";
        case TypingAction::InsertDStrokeUpper: return "InsertDStrokeUpper";
        case TypingAction::InsertECircumflex: return "InsertECircumflex";
        case TypingAction::InsertECircumflexUpper: return "InsertECircumflexUpper";
        case TypingAction::InsertOCircumflex: return "InsertOCircumflex";
        case TypingAction::InsertOCircumflexUpper: return "InsertOCircumflexUpper";
        case TypingAction::InsertOHorn: return "InsertOHorn";
        case TypingAction::InsertOHornUpper: return "InsertOHornUpper";
        case TypingAction::InsertUHorn: return "InsertUHorn";
        case TypingAction::InsertUHornUpper: return "InsertUHornUpper";
    }
    return "None";
}

[[nodiscard]] inline TypingAction StringToTypingAction(std::string_view s) noexcept {
    if (s == "ClearTone") return TypingAction::ClearTone;
    if (s == "ToneAcute") return TypingAction::ToneAcute;
    if (s == "ToneGrave") return TypingAction::ToneGrave;
    if (s == "ToneHook") return TypingAction::ToneHook;
    if (s == "ToneTilde") return TypingAction::ToneTilde;
    if (s == "ToneDot") return TypingAction::ToneDot;
    if (s == "CircumflexA") return TypingAction::CircumflexA;
    if (s == "CircumflexE") return TypingAction::CircumflexE;
    if (s == "CircumflexO") return TypingAction::CircumflexO;
    if (s == "HornW") return TypingAction::HornW;
    if (s == "HornInsertO") return TypingAction::HornInsertO;
    if (s == "HornInsertU") return TypingAction::HornInsertU;
    if (s == "StrokeD") return TypingAction::StrokeD;
    if (s == "VniCircumflex") return TypingAction::VniCircumflex;
    if (s == "VniHorn") return TypingAction::VniHorn;
    if (s == "VniBreve") return TypingAction::VniBreve;
    if (s == "VniStroke") return TypingAction::VniStroke;
    if (s == "HornOrInsertU") return TypingAction::HornOrInsertU;
    if (s == "HornOrInsertUNoStart") return TypingAction::HornOrInsertUNoStart;
    if (s == "UndoAllMarks") return TypingAction::UndoAllMarks;
    if (s == "InsertABreve") return TypingAction::InsertABreve;
    if (s == "InsertABreveUpper") return TypingAction::InsertABreveUpper;
    if (s == "InsertACircumflex") return TypingAction::InsertACircumflex;
    if (s == "InsertACircumflexUpper") return TypingAction::InsertACircumflexUpper;
    if (s == "InsertDStroke") return TypingAction::InsertDStroke;
    if (s == "InsertDStrokeUpper") return TypingAction::InsertDStrokeUpper;
    if (s == "InsertECircumflex") return TypingAction::InsertECircumflex;
    if (s == "InsertECircumflexUpper") return TypingAction::InsertECircumflexUpper;
    if (s == "InsertOCircumflex") return TypingAction::InsertOCircumflex;
    if (s == "InsertOCircumflexUpper") return TypingAction::InsertOCircumflexUpper;
    if (s == "InsertOHorn") return TypingAction::InsertOHorn;
    if (s == "InsertOHornUpper") return TypingAction::InsertOHornUpper;
    if (s == "InsertUHorn") return TypingAction::InsertUHorn;
    if (s == "InsertUHornUpper") return TypingAction::InsertUHornUpper;
    return TypingAction::None;
}

}  // namespace NextKey
