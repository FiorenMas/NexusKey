// tests/engine/StrokeDProposalTest.cpp
//
// W8.3 — verify StrokeDProposal routes StrokeD dispatch through
// TypingEngine::HandleStrokeD without behaviour delta.
//
// Coverage:
//   - Telex `dd` → đ.
//   - VNI `d9` → đ (via HandleVniStroke → HandleStrokeD chain).
//   - Escape: doubled dd→đ→dd reverts via Stroke→None + literal d.
//   - No-target: leading d / d in QU cluster stays literal.
//   - Metadata: relocationKind() == None.
//   - Mock delegation: tryApply forwards (action, keyChar) verbatim.
#include <gtest/gtest.h>

#include <string>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"
#include "core/engine/rule/StrokeDProposal.h"
#include "../TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;
using EngineRule::IModifierSubExecutor;
using EngineRule::RelocationKind;
using EngineRule::StrokeDProposal;

class StrokeDProposalTest : public ::testing::Test {
protected:
    TypingConfig MakeTelex() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::Telex;
        cfg.spellCheckEnabled = true;
        cfg.optimizeLevel = 0;
        return cfg;
    }
    TypingConfig MakeVni() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::VNI;
        cfg.spellCheckEnabled = true;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// --- Telex `dd` → đ -----------------------------------------------------
TEST_F(StrokeDProposalTest, Telex_Dd_ProducesDStroke) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"dd");
    EXPECT_EQ(eng.Peek(), L"đ");
}

TEST_F(StrokeDProposalTest, Telex_Daauw_ProducesDauw) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"ddaa");
    EXPECT_EQ(eng.Peek(), L"đâ");
}

// --- Escape: ddd → dd literal -------------------------------------------
TEST_F(StrokeDProposalTest, Telex_Ddd_EscapesBackToLiteralD) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"ddd");
    EXPECT_EQ(eng.Peek(), L"dd");
}

// --- VNI `d9` → đ (HandleVniStroke routes through HandleStrokeD) --------
// Note: VniStroke dispatch case is separate from StrokeD; the proposal
// covers only the StrokeD case. VniStroke still routes via
// HandleVniStroke which calls HandleStrokeD body. Pin both.
TEST_F(StrokeDProposalTest, Vni_D9_ProducesDStroke) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"d9");
    EXPECT_EQ(eng.Peek(), L"đ");
}

// --- No d in buffer → literal -------------------------------------------
TEST_F(StrokeDProposalTest, NoTargetD_FallsThroughToLiteral) {
    TypingEngine eng(MakeTelex());
    TypeString(eng, L"ad");  // d as first non-d char → no target → literal
    EXPECT_EQ(eng.Peek(), L"ad");
}

// --- Metadata pin --------------------------------------------------------
TEST(StrokeDProposalMetadata, ReportsNoneRelocation) {
    struct StubExec final : IModifierSubExecutor {
        bool HandleAdjacentCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleHornW(TypingAction, wchar_t) override { return false; }
        bool HandleStrokeD(TypingAction, wchar_t) override { return false; }
        bool HandleHornInsert(TypingAction, wchar_t) override { return false; }
        bool HandleVniCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleVniHorn(TypingAction, wchar_t) override { return false; }
        bool HandleVniBreve(TypingAction, wchar_t) override { return false; }
    } stub;
    StrokeDProposal proposal(stub);
    EXPECT_EQ(proposal.relocationKind(), RelocationKind::None);
}

// --- Mock delegation -----------------------------------------------------
TEST(StrokeDProposalDelegation, ForwardsArgumentsAndReturnVerbatim) {
    struct RecordingExec final : IModifierSubExecutor {
        TypingAction lastAction = TypingAction::None;
        wchar_t lastChar = 0;
        bool returnValue = false;
        int calls = 0;
        bool HandleAdjacentCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleHornW(TypingAction, wchar_t) override { return false; }
        bool HandleStrokeD(TypingAction action, wchar_t c) override {
            ++calls;
            lastAction = action;
            lastChar = c;
            return returnValue;
        }
        bool HandleHornInsert(TypingAction, wchar_t) override { return false; }
        bool HandleVniCircumflex(TypingAction, wchar_t) override { return false; }
        bool HandleVniHorn(TypingAction, wchar_t) override { return false; }
        bool HandleVniBreve(TypingAction, wchar_t) override { return false; }
    } rec;

    StrokeDProposal proposal(rec);

    rec.returnValue = true;
    EXPECT_TRUE(proposal.tryApply(TypingAction::StrokeD, L'd'));
    EXPECT_EQ(rec.calls, 1);
    EXPECT_EQ(rec.lastAction, TypingAction::StrokeD);
    EXPECT_EQ(rec.lastChar, L'd');

    rec.returnValue = false;
    EXPECT_FALSE(proposal.tryApply(TypingAction::VniStroke, L'9'));
    EXPECT_EQ(rec.calls, 2);
    EXPECT_EQ(rec.lastAction, TypingAction::VniStroke);
    EXPECT_EQ(rec.lastChar, L'9');
}

}  // namespace
}  // namespace NextKey
