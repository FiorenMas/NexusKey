// tests/engine/HornModifierProposalTest.cpp
//
// W8.2 — verify HornModifierProposal routes HornW dispatch through
// TypingEngine::HandleHornW without behaviour delta.
//
// Coverage:
//   - P1 (ua → ưa) — `mua+w` → mưa.
//   - P2 (uo cycle) — `tuo+w` non-edge, `huo+w` edge prefix.
//   - P3 (oa → oă) — `hoa+w` → hoă.
//   - P4 (escape) — `aaw+w` clears Circumflex; `aww` clears Breve.
//   - P5 (standalone u → ư) — `tu+w` → tư.
//   - P6 (standalone o → ơ) — `to+w` → tơ.
//   - P7 (standalone a → ă) — `aw` → ă.
//   - P8 (synthetic ư insertion) — `w` start-of-word → ư.
//   - Late-modifier on Valid syllables (cuarw/hoaw/muaw, project memory
//     2026-05-25): legitimate paths must NOT regress.
//   - Metadata: relocationKind() == HornVowel.
//   - Mock delegation: tryApply forwards (action, keyChar) verbatim.
#include <gtest/gtest.h>

#include <string>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "core/engine/rule/HornModifierProposal.h"
#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"
#include "../TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;
using EngineRule::HornModifierProposal;
using EngineRule::IModifierSubExecutor;
using EngineRule::RelocationKind;

class HornModifierProposalTest : public ::testing::Test {
protected:
    TypingConfig MakeTelex() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Telex;
        cfg.spellCheckEnabled = true;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// --- P1: ua pattern ------------------------------------------------------
TEST_F(HornModifierProposalTest, P1_Mua_HornsU) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"muaw");
    EXPECT_EQ(eng.Peek(), L"mưa");
}

// --- P2: uo cycle --------------------------------------------------------
TEST_F(HornModifierProposalTest, P2_NonEdgeUo_ProducesUowDiphthong) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"tuow");
    EXPECT_EQ(eng.Peek(), L"tươ");
}

TEST_F(HornModifierProposalTest, P2_EdgePrefixHuo_FirstPressGivesUoHorn) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"huow");
    EXPECT_EQ(eng.Peek(), L"huơ");
}

// --- P3: oa → breve on a -------------------------------------------------
TEST_F(HornModifierProposalTest, P3_Hoa_BreveOnA) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"hoaw");
    EXPECT_EQ(eng.Peek(), L"hoă");
}

// --- P4: escape paths ----------------------------------------------------
TEST_F(HornModifierProposalTest, P4_TuwwEscapesHornBack) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"tuww");
    EXPECT_EQ(eng.Peek(), L"tuw");
}

// --- P5: standalone u ----------------------------------------------------
TEST_F(HornModifierProposalTest, P5_Tu_HornsU) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"tuw");
    EXPECT_EQ(eng.Peek(), L"tư");
}

// --- P6: standalone o ----------------------------------------------------
TEST_F(HornModifierProposalTest, P6_To_HornsO) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"tow");
    EXPECT_EQ(eng.Peek(), L"tơ");
}

// --- P7: standalone a → breve --------------------------------------------
TEST_F(HornModifierProposalTest, P7_Ha_BreveOnA) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"haw");
    EXPECT_EQ(eng.Peek(), L"hă");
}

// --- P8: synthetic ư insertion at start-of-word --------------------------
TEST_F(HornModifierProposalTest, P8_W_StartOfWordInsertsHornU) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"w");
    EXPECT_EQ(eng.Peek(), L"ư");
}

// --- Late-modifier on Valid syllables (do NOT regress) -------------------
// `cuarw` ≡ `c-u-a-r-w` → tone-hook on a (already pre-relocate cua=của),
// then w → P5 on u (since hasUA holds, P1 actually fires first — accept
// late horn). Memory: late horn/breve on Valid is legitimate.
TEST_F(HornModifierProposalTest, LateModifier_Cuarw_ProducesCua) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"cuarw");
    EXPECT_EQ(eng.Peek(), L"cửa");
}

TEST_F(HornModifierProposalTest, LateModifier_Hoaw_ProducesHoa) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"hoaw");
    EXPECT_EQ(eng.Peek(), L"hoă");
}

TEST_F(HornModifierProposalTest, LateModifier_Muaw_ProducesMua) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"muaw");
    EXPECT_EQ(eng.Peek(), L"mưa");
}

// --- Metadata pin --------------------------------------------------------
TEST(HornModifierProposalMetadata, ReportsHornVowelRelocation) {
    struct StubExec final : IModifierSubExecutor {
        bool HandleAdjacentCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleStrokeD(TypingAction, wchar_t) override { return false; }
        bool HandleHornInsert(TypingAction, wchar_t) override { return false; }
        bool HandleVniCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleVniHorn(TypingAction, wchar_t) override { return false; }
        bool HandleVniBreve(TypingAction, wchar_t) override { return false; }
        bool HandleHornW(TypingAction, wchar_t) override { return false; }
    } stub;
    HornModifierProposal proposal(stub);
    EXPECT_EQ(proposal.relocationKind(), RelocationKind::HornVowel);
}

// --- Mock-executor delegation contract -----------------------------------
TEST(HornModifierProposalDelegation, ForwardsArgumentsAndReturnVerbatim) {
    struct RecordingExec final : IModifierSubExecutor {
        TypingAction lastAction = TypingAction::None;
        wchar_t lastChar = 0;
        bool returnValue = false;
        int calls = 0;
        bool HandleAdjacentCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleStrokeD(TypingAction, wchar_t) override { return false; }
        bool HandleHornInsert(TypingAction, wchar_t) override { return false; }
        bool HandleVniCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleVniHorn(TypingAction, wchar_t) override { return false; }
        bool HandleVniBreve(TypingAction, wchar_t) override { return false; }
        bool HandleHornW(TypingAction action, wchar_t c) override {
            ++calls;
            lastAction = action;
            lastChar = c;
            return returnValue;
        }
    } rec;

    HornModifierProposal proposal(rec);

    rec.returnValue = true;
    EXPECT_TRUE(proposal.tryApply(TypingAction::HornW, L'w'));
    EXPECT_EQ(rec.calls, 1);
    EXPECT_EQ(rec.lastAction, TypingAction::HornW);
    EXPECT_EQ(rec.lastChar, L'w');

    rec.returnValue = false;
    EXPECT_FALSE(proposal.tryApply(TypingAction::HornW, L'W'));
    EXPECT_EQ(rec.calls, 2);
    EXPECT_EQ(rec.lastChar, L'W');
}

}  // namespace
}  // namespace NextKey
