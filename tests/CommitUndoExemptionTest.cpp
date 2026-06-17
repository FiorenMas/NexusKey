// VKey — IsCommitUndoExemptKey unit tests (Linux-portable)
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pin contract for the exemption rule shared by HookEngine's
// commit-undo Primed-branch cancel sites:
//   1. Synth-guard cancel (HookEngine.cpp ~line 1061) — wipes commitStack_.
//   2. Catch-all else (HookEngine.cpp ~line 1124) — demotes state to Idle.
//
// History:
//   - Original rule (pre-2026-05-17): only Telex `s/f/r/x/j` + VNI `1-5`
//     were exempt (Sprint 2 D1, chaos 5.3 fix).
//   - 2026-05-17: ESC restore-raw added. Without exemption ESC post-BS
//     hit either cancel site (depending on synth-pending state),
//     defeating the post-BS rawInput restore path. Bug observed on
//     Notepad++ (log session 22:56 → 22:59 after fix).
//   - 2026-05-21a: SimpleTelex tones + UserDefined customKeyMap tone
//     lookup added (commit 002acb9). `khoong+space+BS+s` produced
//     `khôngs` on SimpleTelex because list omitted that method.
//   - 2026-05-21b (this work): Broadened to ALL Telex modifier letters
//     (s/f/r/x/j/z/a/e/o/w/d) and ALL VNI digits (0-9). `ther+space+BS+e`
//     on Telex produced `thẻe` because `e` wasn't in the tone-only list,
//     even though it's a circumflex modifier that "modifies previous
//     word" (engine's ee→ê transform on replay). UserDefined now
//     consults IsCommitUndoExemptAction (any tone/modifier action).

#include <gtest/gtest.h>

#include "core/CommitUndoExemption.h"

namespace NextKey {
namespace {

constexpr uint32_t kVkA      = 0x41;
constexpr uint32_t kVkB      = 0x42;
constexpr uint32_t kVkD      = 0x44;
constexpr uint32_t kVkE      = 0x45;
constexpr uint32_t kVkF      = 0x46;
constexpr uint32_t kVkJ      = 0x4A;
constexpr uint32_t kVkK      = 0x4B;
constexpr uint32_t kVkO      = 0x4F;
constexpr uint32_t kVkR      = 0x52;
constexpr uint32_t kVkS      = 0x53;
constexpr uint32_t kVkW      = 0x57;
constexpr uint32_t kVkX      = 0x58;
constexpr uint32_t kVkZ      = 0x5A;
constexpr uint32_t kVk0      = 0x30;
constexpr uint32_t kVk1      = 0x31;
constexpr uint32_t kVk5      = 0x35;
constexpr uint32_t kVk6      = 0x36;
constexpr uint32_t kVk9      = 0x39;
constexpr uint32_t kVkEscape = 0x1B;
constexpr uint32_t kVkSpace  = 0x20;
constexpr uint32_t kVkReturn = 0x0D;

// ============================================================
// Telex modifier letters — tone (s/f/r/x/j/z) + circumflex (a/e/o)
// + horn (w) + đ-stroke (d). Exempt in Telex/SimpleTelex/Combined.
// ============================================================

constexpr uint32_t kTelexModifierVks[] = {
    kVkS, kVkF, kVkR, kVkX, kVkJ, kVkZ,   // tones (s/f/r/x/j/z)
    kVkA, kVkE, kVkO,                     // circumflex (aa→â, ee→ê, oo→ô)
    kVkW,                                 // horn (w)
    kVkD,                                 // đ-stroke (dd→đ)
};

TEST(CommitUndoExemption, TelexModifierLetters_ExemptInTelex) {
    for (uint32_t vk : kTelexModifierVks) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::Telex, false, false))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, TelexModifierLetters_ExemptInSimpleTelex) {
    // SimpleTelex routes the same letters through `IsTelexMode()` in the
    // engine — only bracket / standalone-`w` semantics differ. All eleven
    // modifier letters must be exempt to allow post-BS replay.
    for (uint32_t vk : kTelexModifierVks) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::SimpleTelex, false, false))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, TelexModifierLetters_ExemptInCombined) {
    for (uint32_t vk : kTelexModifierVks) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::Combined, false, false));
    }
}

TEST(CommitUndoExemption, TelexModifierLetters_NotExemptInVni) {
    // In VNI, letters are literal — only digits modify the previous word.
    for (uint32_t vk : kTelexModifierVks) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::VNI, false, false))
            << "vk=0x" << std::hex << vk << " — literal in VNI";
    }
}

TEST(CommitUndoExemption, TelexModifierLetters_NotExemptInUserDefined) {
    // UserDefined doesn't use the hardcoded list — caller must pass
    // isCustomModifier via customKeyMap lookup. Without that flag,
    // every letter (including 's/e/w/d') is treated as new-word intent.
    for (uint32_t vk : kTelexModifierVks) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::UserDefined, false, false));
    }
}

TEST(CommitUndoExemption, Vni_Digits_NotExemptInSimpleTelex) {
    // SimpleTelex inherits Telex semantics — digits are not modifiers there.
    for (uint32_t vk = kVk0; vk <= kVk9; ++vk) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::SimpleTelex, false, false));
    }
}

// ============================================================
// UserDefined customKeyMap modifier lookup — caller passes
// `isCustomModifier = IsCommitUndoExemptAction(customKeyMap[ch])`;
// predicate trusts that flag in UserDefined mode only.
// ============================================================

TEST(CommitUndoExemption, UserDefined_CustomModifier_Exempt) {
    // Any vk can be exempt in UserDefined when isCustomModifier=true.
    // The predicate doesn't validate WHICH vk — that's the caller's job
    // via the customKeyMap lookup.
    for (uint32_t vk : {kVkS, kVkF, kVkR, kVkX, kVkJ, kVkA, kVkB,
                        kVk1, kVk5, kVk6, kVk0}) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::UserDefined,
                                          /*shift=*/false, /*esc=*/false,
                                          /*isCustomModifier=*/true))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, UserDefined_CustomModifier_FlagIgnoredOutsideUserDefined) {
    // isCustomModifier only applies in UserDefined mode. Setting it true
    // in Telex/VNI/Combined must not change exemption (those use their
    // hardcoded modifier sets).
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::Telex, false, false,
                                       /*isCustomModifier=*/true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::VNI, false, false,
                                       /*isCustomModifier=*/true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::SimpleTelex, false, false,
                                       /*isCustomModifier=*/true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::Combined, false, false,
                                       /*isCustomModifier=*/true));
}

TEST(CommitUndoExemption, UserDefined_NoCustomModifier_NotExempt) {
    // When caller's customKeyMap lookup returns a non-modifier action,
    // isCustomModifier=false → no exemption.
    for (uint32_t vk : {kVkS, kVkA, kVk1, kVkB}) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::UserDefined,
                                           false, false,
                                           /*isCustomModifier=*/false));
    }
}

// ============================================================
// VNI digits — exempt in VNI / Combined when Shift NOT held
// ============================================================

TEST(CommitUndoExemption, Vni_Digits1To5_ExemptInVni) {
    for (uint32_t vk = kVk1; vk <= kVk5; ++vk) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::VNI, false, false))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, Vni_Digits1To5_ExemptInCombined) {
    for (uint32_t vk = kVk1; vk <= kVk5; ++vk) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::Combined, false, false));
    }
}

TEST(CommitUndoExemption, Vni_AllDigitsWithShift_NotExempt) {
    // Shift+digit yields punctuation on US layout — not a tone keystroke.
    for (uint32_t vk = kVk0; vk <= kVk9; ++vk) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::VNI, /*shift=*/true, false))
            << "Shift+vk=0x" << std::hex << vk << " is punctuation, must not be exempt";
    }
}

TEST(CommitUndoExemption, Vni_Digit0_ExemptInVni) {
    // 2026-05-21b: VNI '0' is clear-tone — same semantic class as tone
    // modifiers ("modifies previous word"). After broadening, all digits
    // 0-9 are exempt in VNI/Combined.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVk0, InputMethod::VNI, false, false));
}

TEST(CommitUndoExemption, Vni_Digits6To9_ExemptInVni) {
    // 2026-05-21b: VNI '6/7/8/9' are vowel/consonant modifier keys
    // (circumflex, horn, breve, stroke). They reshape the previous letter
    // — same semantic class as tones. Now exempt to enable post-BS replay.
    for (uint32_t vk = kVk6; vk <= kVk9; ++vk) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::VNI, false, false))
            << "vk=0x" << std::hex << vk;
    }
}

TEST(CommitUndoExemption, Vni_AllDigits_ExemptInCombined) {
    for (uint32_t vk = kVk0; vk <= kVk9; ++vk) {
        EXPECT_TRUE(IsCommitUndoExemptKey(vk, InputMethod::Combined, false, false));
    }
}

TEST(CommitUndoExemption, Vni_Digits_NotExemptInTelex) {
    for (uint32_t vk = kVk1; vk <= kVk5; ++vk) {
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::Telex, false, false))
            << "In Telex, digits are not tone keys";
    }
}

// ============================================================
// ESC restore-raw — exempt when escIsCancelTrigger is on
// (snapshot of HotkeyRegistry::Matches(Intent::CancelComposition, VK_ESCAPE...))
// ============================================================

TEST(CommitUndoExemption, Esc_ExemptWhenToggleOn_AllMethods) {
    for (auto m : {InputMethod::Telex, InputMethod::VNI,
                   InputMethod::Combined, InputMethod::UserDefined}) {
        EXPECT_TRUE(IsCommitUndoExemptKey(kVkEscape, m, false, /*escEnabled=*/true))
            << "method=" << static_cast<int>(m);
    }
}

TEST(CommitUndoExemption, Esc_NotExemptWhenToggleOff) {
    for (auto m : {InputMethod::Telex, InputMethod::VNI,
                   InputMethod::Combined, InputMethod::UserDefined}) {
        EXPECT_FALSE(IsCommitUndoExemptKey(kVkEscape, m, false, /*escEnabled=*/false));
    }
}

TEST(CommitUndoExemption, Esc_ShiftDoesNotChangeExemption) {
    // Shift is consulted only for VNI digits. ESC ignores shift.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkEscape, InputMethod::Telex,
                                       /*shift=*/true, /*escEnabled=*/true));
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkEscape, InputMethod::Telex,
                                       /*shift=*/false, /*escEnabled=*/true));
}

// ============================================================
// Non-exempt keys — must always return false
// ============================================================

TEST(CommitUndoExemption, NonModifierLetters_NotExempt) {
    // Letters outside the Telex modifier set (s/f/r/x/j/z/a/e/o/w/d).
    // Examples: b, c, g, h, i, k, l, m, n, p, q, t, u, v, y. These are
    // pure literals — typing them after BS legitimately starts a new
    // word, no replay needed.
    const uint32_t modifierSet[] = {
        kVkS, kVkF, kVkR, kVkX, kVkJ, kVkZ,
        kVkA, kVkE, kVkO, kVkW, kVkD,
    };
    auto isModifier = [&](uint32_t vk) {
        for (uint32_t m : modifierSet) if (m == vk) return true;
        return false;
    };
    for (uint32_t vk = kVkA; vk <= kVkA + 25u; ++vk) {
        if (isModifier(vk)) continue;
        EXPECT_FALSE(IsCommitUndoExemptKey(vk, InputMethod::Telex, false, true))
            << "vk=0x" << std::hex << vk
            << " is not a Telex modifier; must NOT be exempt";
    }
}

TEST(CommitUndoExemption, NonAlphaKeys_NotExempt) {
    // Space, Enter, ordinary letter B — none should match exemption.
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkSpace, InputMethod::Telex, false, true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkReturn, InputMethod::Telex, false, true));
    EXPECT_FALSE(IsCommitUndoExemptKey(kVkB, InputMethod::Telex, false, true));
}

// ============================================================
// IsCommitUndoExemptAction — semantic class predicate consumed by
// UserDefined customKeyMap lookup at the HookEngine call site.
// ============================================================

TEST(CommitUndoExemptionAction, ToneActionsExempt) {
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::ClearTone));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::ToneAcute));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::ToneGrave));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::ToneHook));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::ToneTilde));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::ToneDot));
}

TEST(CommitUndoExemptionAction, TelexModifierActionsExempt) {
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::CircumflexA));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::CircumflexE));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::CircumflexO));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::HornW));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::HornInsertO));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::HornInsertU));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::StrokeD));
}

TEST(CommitUndoExemptionAction, VniModifierActionsExempt) {
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::VniCircumflex));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::VniHorn));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::VniBreve));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::VniStroke));
}

TEST(CommitUndoExemptionAction, BorderlineModifiersExempt) {
    // HornOrInsertU + variants and UndoAllMarks all "modify previous word"
    // when there's a target — they fall in the same semantic class.
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::HornOrInsertU));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::HornOrInsertUNoStart));
    EXPECT_TRUE(IsCommitUndoExemptAction(TypingAction::UndoAllMarks));
}

TEST(CommitUndoExemptionAction, DirectInsertActionsNotExempt) {
    // Insert* synthesise a fresh char rather than modify the prior word.
    // Treated as new-word intent.
    EXPECT_FALSE(IsCommitUndoExemptAction(TypingAction::None));
    EXPECT_FALSE(IsCommitUndoExemptAction(TypingAction::InsertABreve));
    EXPECT_FALSE(IsCommitUndoExemptAction(TypingAction::InsertACircumflex));
    EXPECT_FALSE(IsCommitUndoExemptAction(TypingAction::InsertDStroke));
    EXPECT_FALSE(IsCommitUndoExemptAction(TypingAction::InsertECircumflex));
    EXPECT_FALSE(IsCommitUndoExemptAction(TypingAction::InsertOCircumflex));
    EXPECT_FALSE(IsCommitUndoExemptAction(TypingAction::InsertOHorn));
    EXPECT_FALSE(IsCommitUndoExemptAction(TypingAction::InsertUHorn));
}

// ============================================================
// Cross-cut regression cases
// ============================================================

TEST(CommitUndoExemption, Regression_2026_05_21b_TelexPostBSCircumflex) {
    // Repro: user typed `ther + space + BS + e` on Telex method in
    // Chrome (Electron WebView2). Expected `thể` — engine on replay
    // pushes 't','h','e','r' → "thẻ", then 'e' triggers ee→ê transform
    // preserving hook tone → "thể". Got `thẻe` because `e` wasn't in
    // the tone-only exempt list. Hook log:
    //   "commit-undo: drop stack-top 'thẻ' for non-tone alpha 'E'
    //    → fresh composition"
    // Fix: broaden exempt to all Telex modifier letters (s/f/r/x/j/z/
    // a/e/o/w/d), not just the 5 tone keys.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkE, InputMethod::Telex,
                                       /*shift=*/false, /*esc=*/false))
        << "'e' must be exempt — it's a Telex circumflex modifier (ee→ê).";
    // Same regression also fires for o/a (circumflex) and w/d/z.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkO, InputMethod::Telex, false, false));
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkA, InputMethod::Telex, false, false));
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkW, InputMethod::Telex, false, false));
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkD, InputMethod::Telex, false, false));
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkZ, InputMethod::Telex, false, false));
}

TEST(CommitUndoExemption, Regression_2026_05_21_SimpleTelexPostBSTone) {
    // Repro: user typed `khoong + space + BS + s` on SimpleTelex method
    // in Notepad (Win32, 30ms settle). Expected `khống` (sắc on ô).
    // Got `khôngs`. Hook log showed:
    //   "commit-undo: drop stack-top 'không' for non-tone alpha 'S'
    //    → fresh composition"
    // Root cause: IsCommitUndoExemptKey only checked Telex + Combined
    // for tone modifiers, omitting SimpleTelex. Engine treats SimpleTelex
    // tone keys identically (IsTelexMode()), so the cancel branch
    // mismatched the engine's classification and dropped the stack.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkS, InputMethod::SimpleTelex,
                                       /*shift=*/false, /*esc=*/false))
        << "After SimpleTelex bug fix, 's' must be exempt so the "
           "alpha-branch replay path fires.";
}

TEST(CommitUndoExemption, Regression_2026_05_21_UserDefinedCustomTone) {
    // Same flow as above but on UserDefined with customKeyMap bound
    // (e.g. 's' → ToneAcute mimicking Telex). The caller computes
    // isCustomModifier via customKeyMap lookup; predicate honours it.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkS, InputMethod::UserDefined,
                                       /*shift=*/false, /*esc=*/false,
                                       /*isCustomModifier=*/true))
        << "UserDefined with customKeyMap['s']=ToneAcute must allow "
           "post-BS replay just like Telex.";
}

TEST(CommitUndoExemption, Regression_2026_05_17_EscPostBS) {
    // Scenario: user typed `virus → space → BS → ESC`. Without ESC exemption,
    // the catch-all else demotes state from Primed to Idle, and
    // HandlePreDispatch's ESC branch (`hasPrimedCommit` condition) fails.
    // Log signature: BS log line shows `→ Primed`, then no `EscRestoreRaw[post-BS]`
    // line on ESC. Fix commits: 41ba120 + 351defa.
    EXPECT_TRUE(IsCommitUndoExemptKey(kVkEscape, InputMethod::Telex,
                                       /*shift=*/false, /*escEnabled=*/true))
        << "ESC must be exempt when bound to CancelComposition — otherwise the "
           "catch-all demote-to-Idle wipes Primed state before TryEscRestoreRaw runs.";
}

}  // namespace
}  // namespace NextKey
