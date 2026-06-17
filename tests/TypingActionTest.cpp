// VKey - TypingAction Tests
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Unit tests for ClassifyKey — verifies key→action mapping per mode.

#include "core/engine/TypingAction.h"
#include <gtest/gtest.h>

using NextKey::TypingAction;
using NextKey::ClassifyKey;

namespace {

// Helper to keep callsites readable.
constexpr TypingAction Telex(wchar_t lower) {
    return ClassifyKey(lower, /*isTelex=*/true, /*isVni=*/false);
}
constexpr TypingAction Vni(wchar_t lower) {
    return ClassifyKey(lower, /*isTelex=*/false, /*isVni=*/true);
}
constexpr TypingAction Combined(wchar_t lower) {
    return ClassifyKey(lower, /*isTelex=*/true, /*isVni=*/true);
}

}  // namespace

// -- Telex tone keys --
TEST(TypingActionClassifyKey, TelexToneKeys) {
    EXPECT_EQ(Telex(L'z'), TypingAction::ClearTone);
    EXPECT_EQ(Telex(L's'), TypingAction::ToneAcute);
    EXPECT_EQ(Telex(L'f'), TypingAction::ToneGrave);
    EXPECT_EQ(Telex(L'r'), TypingAction::ToneHook);
    EXPECT_EQ(Telex(L'x'), TypingAction::ToneTilde);
    EXPECT_EQ(Telex(L'j'), TypingAction::ToneDot);
}

// -- Telex modifier keys --
TEST(TypingActionClassifyKey, TelexModifierKeys) {
    EXPECT_EQ(Telex(L'a'), TypingAction::CircumflexA);
    EXPECT_EQ(Telex(L'e'), TypingAction::CircumflexE);
    EXPECT_EQ(Telex(L'o'), TypingAction::CircumflexO);
    EXPECT_EQ(Telex(L'w'), TypingAction::HornW);
    EXPECT_EQ(Telex(L'['), TypingAction::HornInsertO);
    EXPECT_EQ(Telex(L']'), TypingAction::HornInsertU);
    EXPECT_EQ(Telex(L'd'), TypingAction::StrokeD);
}

// -- VNI tone keys --
TEST(TypingActionClassifyKey, VniToneKeys) {
    EXPECT_EQ(Vni(L'0'), TypingAction::ClearTone);
    EXPECT_EQ(Vni(L'1'), TypingAction::ToneAcute);
    EXPECT_EQ(Vni(L'2'), TypingAction::ToneGrave);
    EXPECT_EQ(Vni(L'3'), TypingAction::ToneHook);
    EXPECT_EQ(Vni(L'4'), TypingAction::ToneTilde);
    EXPECT_EQ(Vni(L'5'), TypingAction::ToneDot);
}

// -- VNI modifier keys --
TEST(TypingActionClassifyKey, VniModifierKeys) {
    EXPECT_EQ(Vni(L'6'), TypingAction::VniCircumflex);
    EXPECT_EQ(Vni(L'7'), TypingAction::VniHorn);
    EXPECT_EQ(Vni(L'8'), TypingAction::VniBreve);
    EXPECT_EQ(Vni(L'9'), TypingAction::VniStroke);
}

// -- Mode isolation: VNI digits are inert in Telex-only mode --
TEST(TypingActionClassifyKey, TelexOnlyIgnoresVniDigits) {
    EXPECT_EQ(Telex(L'1'), TypingAction::None);
    EXPECT_EQ(Telex(L'6'), TypingAction::None);
    EXPECT_EQ(Telex(L'9'), TypingAction::None);
    EXPECT_EQ(Telex(L'0'), TypingAction::None);
}

// -- Mode isolation: Telex letters are inert in VNI-only mode --
TEST(TypingActionClassifyKey, VniOnlyIgnoresTelexLetters) {
    EXPECT_EQ(Vni(L's'), TypingAction::None);
    EXPECT_EQ(Vni(L'w'), TypingAction::None);
    EXPECT_EQ(Vni(L'a'), TypingAction::None);
    EXPECT_EQ(Vni(L'd'), TypingAction::None);
    EXPECT_EQ(Vni(L'['), TypingAction::None);
    EXPECT_EQ(Vni(L']'), TypingAction::None);
    EXPECT_EQ(Vni(L'z'), TypingAction::None);
}

// -- Combined mode: Telex letters + VNI digits both classify --
TEST(TypingActionClassifyKey, CombinedHandlesBothLetterAndDigit) {
    EXPECT_EQ(Combined(L's'), TypingAction::ToneAcute);
    EXPECT_EQ(Combined(L'1'), TypingAction::ToneAcute);
    EXPECT_EQ(Combined(L'a'), TypingAction::CircumflexA);
    EXPECT_EQ(Combined(L'6'), TypingAction::VniCircumflex);
    EXPECT_EQ(Combined(L'w'), TypingAction::HornW);
    EXPECT_EQ(Combined(L'7'), TypingAction::VniHorn);
}

// -- Unmapped chars classify as None in every mode --
TEST(TypingActionClassifyKey, UnmappedCharsAreNone) {
    for (wchar_t c : {L'b', L'c', L'g', L'h', L'i', L'k', L'l', L'm',
                       L'n', L'p', L'q', L't', L'u', L'v', L'y',
                       L' ', L'.', L',', L'\n'}) {
        EXPECT_EQ(Telex(c), TypingAction::None) << "Telex c=" << (int)c;
        EXPECT_EQ(Vni(c), TypingAction::None) << "VNI c=" << (int)c;
        EXPECT_EQ(Combined(c), TypingAction::None) << "Combined c=" << (int)c;
    }
}

// -- All-modes-off returns None for everything --
TEST(TypingActionClassifyKey, AllModesOffAlwaysReturnsNone) {
    auto Off = [](wchar_t c) {
        return ClassifyKey(c, /*isTelex=*/false, /*isVni=*/false);
    };
    EXPECT_EQ(Off(L's'), TypingAction::None);
    EXPECT_EQ(Off(L'1'), TypingAction::None);
    EXPECT_EQ(Off(L'a'), TypingAction::None);
    EXPECT_EQ(Off(L'w'), TypingAction::None);
}

// -- ClassifyKey is constexpr — verify compile-time evaluation --
TEST(TypingActionClassifyKey, ConstexprEvaluation) {
    constexpr TypingAction acute = Telex(L's');
    constexpr TypingAction horn = Telex(L'w');
    constexpr TypingAction none = Telex(L'b');
    static_assert(acute == TypingAction::ToneAcute);
    static_assert(horn == TypingAction::HornW);
    static_assert(none == TypingAction::None);
    SUCCEED();
}
