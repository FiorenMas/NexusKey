// Tests for DecideDigitLed — pure "digit-led word" state machine.
// HookEngine.cpp + tsf/EngineController.cpp are Windows-only, so we test
// the extracted decision in isolation (same pattern as CjkSwitchDecisionTest,
// AutoCapDecisionTest, AutoCapStateTransitionTest).
//
// Modifier handling is NOT tested here — the caller (HookEngine DispatchKeyAction
// step 5 / TSF EngineController WantKey step 1) early-returns on Ctrl/Alt/Win
// before reaching DecideDigitLed. Only Shift is forwarded as an input field
// because Shift+digit produces punctuation (!@#…) and must not arm.
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include "core/DigitLedWordDecision.h"

using NextKey::DecideDigitLed;
using NextKey::DigitLedDecision;
using NextKey::DigitLedInputs;
using NextKey::InputMethod;
using NextKey::IsDigitLedBoundary;
namespace Vk = NextKey::DigitLedVk;

namespace {

// Default fresh state: VNI mode, engine empty, no shift, not armed.
// Individual tests override what they exercise.
DigitLedInputs MakeInputs(uint32_t vk, InputMethod method = InputMethod::VNI) {
    DigitLedInputs in{};
    in.vkCode = vk;
    in.shift = false;
    in.engineEmpty = true;
    in.method = method;
    in.currentlyArmed = false;
    return in;
}

}  // namespace

// ── Arm conditions ──────────────────────────────────────────────────────

TEST(DigitLedWord, DigitAtWordStart_VNI_Arms) {
    auto in = MakeInputs(Vk::kDigit0 + 6);  // '6'
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Arm);
}

TEST(DigitLedWord, DigitAtWordStart_Combined_Arms) {
    auto in = MakeInputs(Vk::kDigit0 + 7, InputMethod::Combined);
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Arm);
}

TEST(DigitLedWord, DigitAtWordStart_UserDefined_Arms) {
    auto in = MakeInputs(Vk::kDigit0 + 1, InputMethod::UserDefined);
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Arm);
}

TEST(DigitLedWord, AllDigits0Through9_Arm) {
    for (uint32_t d = 0; d <= 9; ++d) {
        auto in = MakeInputs(Vk::kDigit0 + d);
        EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Arm)
            << "digit " << d;
    }
}

// ── Arm-suppression conditions ──────────────────────────────────────────

TEST(DigitLedWord, DigitInTelex_DoesNotArm) {
    auto in = MakeInputs(Vk::kDigit0 + 6, InputMethod::Telex);
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Continue);
}

TEST(DigitLedWord, DigitInSimpleTelex_DoesNotArm) {
    auto in = MakeInputs(Vk::kDigit0 + 6, InputMethod::SimpleTelex);
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Continue);
}

TEST(DigitLedWord, ShiftDigit_DoesNotArm) {
    // Shift+digit produces symbols (!@#…), not a digit-led word.
    auto in = MakeInputs(Vk::kDigit0 + 1);
    in.shift = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Continue);
}

TEST(DigitLedWord, DigitMidWord_DoesNotArm) {
    // Engine has buffer (e.g., "ca") — '6' here is VNI tone/modifier, not word-start.
    auto in = MakeInputs(Vk::kDigit0 + 6);
    in.engineEmpty = false;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Continue);
}

TEST(DigitLedWord, LetterAtWordStart_DoesNotArm) {
    auto in = MakeInputs(0x41);  // 'A'
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Continue);
}

TEST(DigitLedWord, PunctuationAtWordStart_DoesNotArm) {
    auto in = MakeInputs(0xBE);  // VK_OEM_PERIOD '.'
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Continue);
}

// ── Bypass while armed ──────────────────────────────────────────────────

TEST(DigitLedWord, ArmedThenLetter_Bypasses) {
    auto in = MakeInputs(0x41);  // 'A'
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Bypass);
}

TEST(DigitLedWord, ArmedThenDigit_Bypasses) {
    // "67…" — second digit continues the digit-led run.
    auto in = MakeInputs(Vk::kDigit0 + 7);
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Bypass);
}

TEST(DigitLedWord, ArmedThenPunctuation_Bypasses) {
    // "6.5" — period continues the run, only whitespace/nav breaks it.
    auto in = MakeInputs(0xBE);  // VK_OEM_PERIOD
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Bypass);
}

TEST(DigitLedWord, ArmedThenShiftLetter_Bypasses) {
    // Shift+letter doesn't reset — still inside the same word.
    auto in = MakeInputs(0x42);  // 'B'
    in.shift = true;
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Bypass);
}

// ── Reset on boundary keys ──────────────────────────────────────────────

TEST(DigitLedWord, ArmedThenSpace_Resets) {
    auto in = MakeInputs(Vk::kSpace);
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Reset);
}

TEST(DigitLedWord, ArmedThenEnter_Resets) {
    auto in = MakeInputs(Vk::kReturn);
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Reset);
}

TEST(DigitLedWord, ArmedThenTab_Resets) {
    auto in = MakeInputs(Vk::kTab);
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Reset);
}

TEST(DigitLedWord, ArmedThenEscape_Resets) {
    auto in = MakeInputs(Vk::kEscape);
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Reset);
}

TEST(DigitLedWord, ArmedThenBackspace_Resets) {
    auto in = MakeInputs(Vk::kBack);
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Reset);
}

TEST(DigitLedWord, ArmedThenDelete_Resets) {
    auto in = MakeInputs(Vk::kDelete);
    in.currentlyArmed = true;
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Reset);
}

TEST(DigitLedWord, ArmedThenAllArrowKeys_Reset) {
    for (uint32_t vk : {Vk::kLeft, Vk::kUp, Vk::kRight, Vk::kDown}) {
        auto in = MakeInputs(vk);
        in.currentlyArmed = true;
        EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Reset)
            << "vk=0x" << std::hex << vk;
    }
}

TEST(DigitLedWord, ArmedThenAllNavKeys_Reset) {
    for (uint32_t vk : {Vk::kHome, Vk::kEnd, Vk::kPrior, Vk::kNext}) {
        auto in = MakeInputs(vk);
        in.currentlyArmed = true;
        EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Reset)
            << "vk=0x" << std::hex << vk;
    }
}

// ── IsDigitLedBoundary direct ───────────────────────────────────────────

TEST(DigitLedBoundary, BoundaryKeys) {
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kSpace));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kReturn));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kTab));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kEscape));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kBack));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kDelete));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kLeft));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kRight));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kUp));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kDown));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kHome));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kEnd));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kPrior));
    EXPECT_TRUE(IsDigitLedBoundary(Vk::kNext));
}

TEST(DigitLedBoundary, NonBoundaryKeys) {
    EXPECT_FALSE(IsDigitLedBoundary(0x41));        // 'A'
    EXPECT_FALSE(IsDigitLedBoundary(Vk::kDigit0)); // '0'
    EXPECT_FALSE(IsDigitLedBoundary(Vk::kDigit9)); // '9'
    EXPECT_FALSE(IsDigitLedBoundary(0xBE));        // VK_OEM_PERIOD '.'
    EXPECT_FALSE(IsDigitLedBoundary(0xBC));        // VK_OEM_COMMA ','
    EXPECT_FALSE(IsDigitLedBoundary(0xBD));        // VK_OEM_MINUS '-'
}

// ── Full word lifecycle (sequence test) ─────────────────────────────────

TEST(DigitLedWord, FullWordSequence_6abcSpace) {
    // Mirrors the dispatch caller pattern: caller maintains the bool,
    // updates it per Decision returned.
    bool armed = false;
    auto step = [&armed](uint32_t vk) -> DigitLedDecision {
        DigitLedInputs in{vk, false, !armed /*engineEmpty proxy*/, InputMethod::VNI, armed};
        auto d = DecideDigitLed(in);
        if (d == DigitLedDecision::Arm) armed = true;
        else if (d == DigitLedDecision::Reset) armed = false;
        return d;
    };

    EXPECT_EQ(step(Vk::kDigit0 + 6), DigitLedDecision::Arm);     // '6' — arm
    EXPECT_TRUE(armed);
    EXPECT_EQ(step(0x41),             DigitLedDecision::Bypass);  // 'A'
    EXPECT_EQ(step(0x42),             DigitLedDecision::Bypass);  // 'B'
    EXPECT_EQ(step(0x43),             DigitLedDecision::Bypass);  // 'C'
    EXPECT_EQ(step(Vk::kSpace),       DigitLedDecision::Reset);   // ' ' — reset
    EXPECT_FALSE(armed);
    EXPECT_EQ(step(0x44),             DigitLedDecision::Continue); // 'D' — fresh word, no arm
    EXPECT_FALSE(armed);
}

TEST(DigitLedWord, NumericSequence_6dot5Space) {
    // "6.5 " stays one digit-led run; only space resets.
    bool armed = false;
    auto step = [&armed](uint32_t vk) -> DigitLedDecision {
        DigitLedInputs in{vk, false, true /*engineEmpty: dispatch never routes to engine while armed*/, InputMethod::VNI, armed};
        auto d = DecideDigitLed(in);
        if (d == DigitLedDecision::Arm) armed = true;
        else if (d == DigitLedDecision::Reset) armed = false;
        return d;
    };

    EXPECT_EQ(step(Vk::kDigit0 + 6), DigitLedDecision::Arm);
    EXPECT_EQ(step(0xBE),             DigitLedDecision::Bypass);  // '.'
    EXPECT_EQ(step(Vk::kDigit0 + 5), DigitLedDecision::Bypass);  // '5'
    EXPECT_EQ(step(Vk::kSpace),       DigitLedDecision::Reset);
    EXPECT_FALSE(armed);
}

TEST(DigitLedWord, BackspaceRecovery_RetypeStartsFresh) {
    // "6" + BS resets digit-led so retyping "abc" composes Vietnamese.
    bool armed = false;
    auto step = [&armed](uint32_t vk, bool engineEmpty) -> DigitLedDecision {
        DigitLedInputs in{vk, false, engineEmpty, InputMethod::VNI, armed};
        auto d = DecideDigitLed(in);
        if (d == DigitLedDecision::Arm) armed = true;
        else if (d == DigitLedDecision::Reset) armed = false;
        return d;
    };

    EXPECT_EQ(step(Vk::kDigit0 + 6, true), DigitLedDecision::Arm);
    EXPECT_TRUE(armed);
    EXPECT_EQ(step(Vk::kBack, true),        DigitLedDecision::Reset);
    EXPECT_FALSE(armed);
    EXPECT_EQ(step(0x41, true),             DigitLedDecision::Continue);  // 'A' — fresh
}

// ── Edge cases ──────────────────────────────────────────────────────────

TEST(DigitLedWord, ZeroIsValidArmKey) {
    // VNI '0' is the clear-tone key; at word start it still arms digit-led
    // (a word literally starting with "0" — e.g., "007" — is English).
    auto in = MakeInputs(Vk::kDigit0);
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Arm);
}

TEST(DigitLedWord, NotArmed_BoundaryKeyReturnsContinue) {
    // Space pressed with no digit-led armed shouldn't trigger Reset —
    // there's nothing to reset. Caller routes to normal commit handling.
    auto in = MakeInputs(Vk::kSpace);
    EXPECT_EQ(DecideDigitLed(in), DigitLedDecision::Continue);
}
