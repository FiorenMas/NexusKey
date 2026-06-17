// tests/engine/VniProposalsTest.cpp
//
// W8.5 — verify VniCircumflexProposal, VniHornProposal, VniBreveProposal
// route their respective ProcessModifier dispatch cases through the
// engine body without behaviour delta. Also verifies the W8.5
// ProcessVniVowelModifier ValidPrefix-gated speculate-relocate fix
// landed correctly (TODO 2026-05-25 → resolved).
//
// Coverage:
//   - VniCircumflex (6) on a/e/o; VniHorn (7) on o/u; VniBreve (8) on a.
//   - The new fix: `vi5e6t → việt`, `ngu2o6n → nguồn`.
//   - Metadata pins: Circumflex = TargetTone, Horn = HornVowel, Breve = TargetTone.
//   - Mock-executor delegation for all three proposals.
#include <gtest/gtest.h>

#include "core/config/TypingConfig.h"
#include "core/engine/TypingAction.h"
#include "core/engine/TypingEngine.h"
#include "core/engine/rule/IModifierSubExecutor.h"
#include "core/engine/rule/ModifierProposal.h"
#include "core/engine/rule/VniBreveProposal.h"
#include "core/engine/rule/VniCircumflexProposal.h"
#include "core/engine/rule/VniHornProposal.h"
#include "../TestHelper.h"

namespace NextKey {
namespace {

using Testing::TypeString;
using EngineRule::IModifierSubExecutor;
using EngineRule::RelocationKind;
using EngineRule::VniBreveProposal;
using EngineRule::VniCircumflexProposal;
using EngineRule::VniHornProposal;

class VniProposalsTest : public ::testing::Test {
protected:
    TypingConfig MakeVni() {
        TypingConfig cfg;
        cfg.inputMethod = InputMethod::VNI;
        cfg.spellCheckEnabled = true;
        cfg.optimizeLevel = 0;
        return cfg;
    }
};

// --- VniCircumflex (6) ---------------------------------------------------
TEST_F(VniProposalsTest, VniCircumflex_A6_ProducesACircumflex) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"a6");
    EXPECT_EQ(eng.Peek(), L"â");
}

TEST_F(VniProposalsTest, VniCircumflex_E6_ProducesECircumflex) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"e6");
    EXPECT_EQ(eng.Peek(), L"ê");
}

TEST_F(VniProposalsTest, VniCircumflex_O6_ProducesOCircumflex) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"o6");
    EXPECT_EQ(eng.Peek(), L"ô");
}

// --- VniHorn (7) ---------------------------------------------------------
TEST_F(VniProposalsTest, VniHorn_U7_ProducesUHorn) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"u7");
    EXPECT_EQ(eng.Peek(), L"ư");
}

TEST_F(VniProposalsTest, VniHorn_O7_ProducesOHorn) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"o7");
    EXPECT_EQ(eng.Peek(), L"ơ");
}

// --- VniBreve (8) --------------------------------------------------------
TEST_F(VniProposalsTest, VniBreve_A8_ProducesABreve) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"a8");
    EXPECT_EQ(eng.Peek(), L"ă");
}

// --- W8.5 fix verification (ValidPrefix tone-mid promotion) --------------
TEST_F(VniProposalsTest, FixVerify_Vi5e6t_PromotesToViet) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"vi5e6t");
    EXPECT_EQ(eng.Peek(), L"việt") << "TODO 2026-05-25 resolved by W8.5";
}

TEST_F(VniProposalsTest, FixVerify_Ngu2o6n_PromotesToNguon) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"ngu2o6n");
    EXPECT_EQ(eng.Peek(), L"nguồn");
}

// --- Typo guard preserved: Valid pre-state still mod-only ---------------
// VNI `cua6` — pre-state {c,u,a} is Valid (`cua` is a word). Mod-only
// validation accepts circumflex on a → `cuâ` (ValidPrefix to cuấp/cuấn).
TEST_F(VniProposalsTest, ValidPreState_Cua6_AcceptsCircumflexMod) {
    TypingEngine eng(MakeVni());
    TypeString(eng, L"cua6");
    EXPECT_EQ(eng.Peek(), L"cuâ");
}

// --- Metadata pins -------------------------------------------------------
//
// Per the Option B classification criterion in ModifierProposal.h enum doc:
// bodies with gated `if (needsRelocate)` relocate calls (or mixed gated +
// unconditional) declare Conditional. All three VNI proposals route through
// ProcessVniVowelModifier (Pass 1 gated, Pass 1.5 unconditional) — they
// match the criterion.
struct AllStubs : IModifierSubExecutor {
    bool HandleAdjacentCircumflex(TypingAction, wchar_t) override { return false; }
    bool HandleHornW(TypingAction, wchar_t) override { return false; }
    bool HandleStrokeD(TypingAction, wchar_t) override { return false; }
    bool HandleHornInsert(TypingAction, wchar_t) override { return false; }
    bool HandleVniCircumflex(TypingAction, wchar_t) override { return false; }
    bool HandleVniHorn(TypingAction, wchar_t) override { return false; }
    bool HandleVniBreve(TypingAction, wchar_t) override { return false; }
};

TEST(VniProposalsMetadata, CircumflexReportsConditional) {
    AllStubs stub;
    VniCircumflexProposal p(stub);
    EXPECT_EQ(p.relocationKind(), RelocationKind::Conditional);
}

// HandleVniHorn calls RelocateToneToTarget on every path (not
// RelocateToneToHornVowel as the initial W8.5 commit claimed). The mix of
// unconditional uo/uu paths and gated generic-fallback path is Conditional
// per the Option B criterion.
TEST(VniProposalsMetadata, HornReportsConditional) {
    AllStubs stub;
    VniHornProposal p(stub);
    EXPECT_EQ(p.relocationKind(), RelocationKind::Conditional);
}

TEST(VniProposalsMetadata, BreveReportsConditional) {
    AllStubs stub;
    VniBreveProposal p(stub);
    EXPECT_EQ(p.relocationKind(), RelocationKind::Conditional);
}

// --- Mock delegation -----------------------------------------------------
TEST(VniProposalsDelegation, CircumflexForwardsVerbatim) {
    struct Rec : AllStubs {
        TypingAction lastAction = TypingAction::None;
        wchar_t lastChar = 0;
        bool ret = true;
        bool HandleVniCircumflex(TypingAction a, wchar_t c) override {
            lastAction = a; lastChar = c; return ret;
        }
    } rec;
    VniCircumflexProposal p(rec);
    EXPECT_TRUE(p.tryApply(TypingAction::VniCircumflex, L'6'));
    EXPECT_EQ(rec.lastAction, TypingAction::VniCircumflex);
    EXPECT_EQ(rec.lastChar, L'6');
}

TEST(VniProposalsDelegation, HornForwardsVerbatim) {
    struct Rec : AllStubs {
        TypingAction lastAction = TypingAction::None;
        wchar_t lastChar = 0;
        bool ret = true;
        bool HandleVniHorn(TypingAction a, wchar_t c) override {
            lastAction = a; lastChar = c; return ret;
        }
    } rec;
    VniHornProposal p(rec);
    EXPECT_TRUE(p.tryApply(TypingAction::VniHorn, L'7'));
    EXPECT_EQ(rec.lastAction, TypingAction::VniHorn);
    EXPECT_EQ(rec.lastChar, L'7');
}

TEST(VniProposalsDelegation, BreveForwardsVerbatim) {
    struct Rec : AllStubs {
        TypingAction lastAction = TypingAction::None;
        wchar_t lastChar = 0;
        bool ret = true;
        bool HandleVniBreve(TypingAction a, wchar_t c) override {
            lastAction = a; lastChar = c; return ret;
        }
    } rec;
    VniBreveProposal p(rec);
    EXPECT_TRUE(p.tryApply(TypingAction::VniBreve, L'8'));
    EXPECT_EQ(rec.lastAction, TypingAction::VniBreve);
    EXPECT_EQ(rec.lastChar, L'8');
}

}  // namespace
}  // namespace NextKey
