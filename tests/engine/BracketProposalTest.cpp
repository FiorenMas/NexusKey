// tests/engine/BracketProposalTest.cpp
//
// W8.4 — verify BracketProposal routes HornInsertO/U dispatch through
// TypingEngine::HandleHornInsert without behaviour delta.
//
// Coverage:
//   - `[` at start-of-word → ơ (HornInsertO).
//   - `]` at start-of-word → ư (HornInsertU).
//   - `th[` mid-word → thơ.
//   - `[[` escape → literal `[`.
//   - SimpleTelex mode → `[` falls through to literal (SimpleTelex omits
//     bracket keys per L861 short-circuit).
//   - Metadata: relocationKind() == None.
//   - Mock delegation: tryApply forwards (action, keyChar) verbatim.
#include <gtest/gtest.h>

#include <string>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "core/engine/rule/BracketProposal.h"
#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"
#include "../TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;
using EngineRule::BracketProposal;
using EngineRule::IModifierSubExecutor;
using EngineRule::RelocationKind;

class BracketProposalTest : public ::testing::Test {
protected:
    TypingConfig MakeTelex() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Telex;
        cfg.spellCheckEnabled = true;
        cfg.optimizeLevel = 0;
        return cfg;
    }
    TypingConfig MakeSimpleTelex() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::SimpleTelex;
        cfg.spellCheckEnabled = true;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// --- HornInsertO `[` -----------------------------------------------------
TEST_F(BracketProposalTest, OpenBracket_AtStart_InsertsOHorn) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"[");
    EXPECT_EQ(eng.Peek(), L"ơ");
}

TEST_F(BracketProposalTest, OpenBracket_MidWord_AppendsOHorn) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"th[");
    EXPECT_EQ(eng.Peek(), L"thơ");
}

// --- HornInsertU `]` -----------------------------------------------------
TEST_F(BracketProposalTest, CloseBracket_AtStart_InsertsUHorn) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"]");
    EXPECT_EQ(eng.Peek(), L"ư");
}

TEST_F(BracketProposalTest, CloseBracket_MidWord_AppendsUHorn) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"th]");
    EXPECT_EQ(eng.Peek(), L"thư");
}

// --- Escape: doubled key reverts to literal -----------------------------
TEST_F(BracketProposalTest, DoubledOpenBracket_EscapesToLiteral) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"[[");
    EXPECT_EQ(eng.Peek(), L"[");
}

TEST_F(BracketProposalTest, DoubledCloseBracket_EscapesToLiteral) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"]]");
    EXPECT_EQ(eng.Peek(), L"]");
}

// --- SimpleTelex skips bracket keys -------------------------------------
TEST_F(BracketProposalTest, SimpleTelex_BracketsFallThrough) {
    TypingEngine eng(MakeSimpleTelex());
    TypeString(eng, L"[");
    EXPECT_EQ(eng.Peek(), L"[")
        << "SimpleTelex L861: HandleHornInsert returns false, literal lands";
}

// --- Metadata pin --------------------------------------------------------
TEST(BracketProposalMetadata, ReportsNoneRelocation) {
    struct StubExec final : IModifierSubExecutor {
        bool HandleAdjacentCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleHornW(TypingAction, wchar_t) override { return false; }
        bool HandleStrokeD(TypingAction, wchar_t) override { return false; }
        bool HandleHornInsert(TypingAction, wchar_t) override { return false; }
        bool HandleVniCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleVniHorn(TypingAction, wchar_t) override { return false; }
        bool HandleVniBreve(TypingAction, wchar_t) override { return false; }
    } stub;
    BracketProposal proposal(stub);
    EXPECT_EQ(proposal.relocationKind(), RelocationKind::None);
}

// --- Mock delegation -----------------------------------------------------
TEST(BracketProposalDelegation, ForwardsArgumentsAndReturnVerbatim) {
    struct RecordingExec final : IModifierSubExecutor {
        TypingAction lastAction = TypingAction::None;
        wchar_t lastChar = 0;
        bool returnValue = false;
        int calls = 0;
        bool HandleAdjacentCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleHornW(TypingAction, wchar_t) override { return false; }
        bool HandleStrokeD(TypingAction, wchar_t) override { return false; }
        bool HandleHornInsert(TypingAction action, wchar_t c) override {
            ++calls;
            lastAction = action;
            lastChar = c;
            return returnValue;
        }
        bool HandleVniCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleVniHorn(TypingAction, wchar_t) override { return false; }
        bool HandleVniBreve(TypingAction, wchar_t) override { return false; }
    } rec;

    BracketProposal proposal(rec);

    rec.returnValue = true;
    EXPECT_TRUE(proposal.tryApply(TypingAction::HornInsertO, L'['));
    EXPECT_EQ(rec.calls, 1);
    EXPECT_EQ(rec.lastAction, TypingAction::HornInsertO);
    EXPECT_EQ(rec.lastChar, L'[');

    rec.returnValue = false;
    EXPECT_FALSE(proposal.tryApply(TypingAction::HornInsertU, L']'));
    EXPECT_EQ(rec.calls, 2);
    EXPECT_EQ(rec.lastAction, TypingAction::HornInsertU);
    EXPECT_EQ(rec.lastChar, L']');
}

}  // namespace
}  // namespace NextKey
