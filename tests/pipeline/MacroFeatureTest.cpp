// tests/pipeline/MacroFeatureTest.cpp
//
// W4b.2 — verify MacroFeature is a thin wrapper that delegates the keystroke
// to an IMacroExecutor and maps MacroOutcome to a Result + a flow-control
// intent (ConsumeKey / PassThrough / none). Also pins the
// (Stage, Priority, Requires) metadata.
#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "core/pipeline/GateMask.h"
#include "core/pipeline/ICompositionSession.h"
#include "core/pipeline/IMacroExecutor.h"
#include "core/pipeline/Intent.h"
#include "core/pipeline/IntentSink.h"
#include "core/pipeline/KeyContext.h"
#include "core/pipeline/MacroFeature.h"
#include "core/pipeline/Result.h"
#include "core/pipeline/Stage.h"

using namespace NextKey::Pipeline;

namespace {

class StubSession final : public ICompositionSession {
public:
    StubSession(std::wstring_view prev, std::wstring_view eng, std::wstring_view raw) noexcept
        : prev_(prev), eng_(eng), raw_(raw) {}

    [[nodiscard]] std::wstring_view PreviousRendered() const noexcept override { return prev_; }
    [[nodiscard]] std::wstring_view EngineRendered()   const noexcept override { return eng_; }
    [[nodiscard]] std::wstring_view RawInput()         const noexcept override { return raw_; }

private:
    std::wstring_view prev_;
    std::wstring_view eng_;
    std::wstring_view raw_;
};

class MockMacroExecutor final : public IMacroExecutor {
public:
    MacroOutcome HandleMacro(std::uint16_t vkCode,
                             bool shift, bool capsLock,
                             bool ctrl, bool alt, bool win) override {
        last_vk_       = vkCode;
        last_shift_    = shift;
        last_capsLock_ = capsLock;
        last_ctrl_     = ctrl;
        last_alt_      = alt;
        last_win_      = win;
        ++calls_;
        return outcome_;
    }

    MacroOutcome      outcome_       = MacroOutcome::Fallthrough;
    std::uint16_t     last_vk_       = 0;
    bool              last_shift_    = false;
    bool              last_capsLock_ = false;
    bool              last_ctrl_     = false;
    bool              last_alt_      = false;
    bool              last_win_      = false;
    int               calls_         = 0;
};

class RecordingSink final : public IntentSink {
public:
    void Emit(Intent intent) override { intents_.push_back(std::move(intent)); }

    std::vector<Intent> intents_;
};

KeyContext makeStubCtx(const ICompositionSession& session,
                       std::uint16_t vk = 0x41,
                       bool shift = false, bool capsLock = false,
                       bool ctrl = false, bool alt = false, bool win = false) {
    return KeyContext{
        .vk = vk, .keyChar = L'\0',
        .shift = shift, .capsLock = capsLock, .ctrl = ctrl, .alt = alt, .win = win,
        .session = &session, .reinjectVk = 0,
    };
}

}  // namespace

TEST(MacroFeature, EatOutcomeEmitsConsumeKeyAndHandled) {
    MockMacroExecutor exec;
    exec.outcome_ = MacroOutcome::Eat;
    MacroFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Handled);
    ASSERT_EQ(sink.intents_.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Intents::ConsumeKey>(sink.intents_[0]));
}

TEST(MacroFeature, PassOutcomeEmitsPassThroughAndVeto) {
    MockMacroExecutor exec;
    exec.outcome_ = MacroOutcome::Pass;
    MacroFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Veto);
    ASSERT_EQ(sink.intents_.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Intents::PassThrough>(sink.intents_[0]));
}

TEST(MacroFeature, FallthroughOutcomeEmitsNothingAndPass) {
    MockMacroExecutor exec;
    exec.outcome_ = MacroOutcome::Fallthrough;
    MacroFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Pass);
    EXPECT_TRUE(sink.intents_.empty());
}

TEST(MacroFeature, NoOpOutcomeEmitsNothingAndPass) {
    MockMacroExecutor exec;
    exec.outcome_ = MacroOutcome::NoOp;
    MacroFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Pass);
    EXPECT_TRUE(sink.intents_.empty());
}

TEST(MacroFeature, PassesAllSixArgsToExecutor) {
    MockMacroExecutor exec;
    exec.outcome_ = MacroOutcome::Fallthrough;
    MacroFeature feature(exec);
    StubSession session(L"", L"", L"");
    // vk=0x41 ('A'), shift=true, capsLock=false, ctrl=false, alt=true, win=false
    KeyContext ctx = makeStubCtx(session,
                                 /*vk=*/0x41,
                                 /*shift=*/true,
                                 /*capsLock=*/false,
                                 /*ctrl=*/false,
                                 /*alt=*/true,
                                 /*win=*/false);
    RecordingSink sink;

    (void)feature.Try(ctx, sink);

    EXPECT_EQ(exec.calls_, 1);
    EXPECT_EQ(exec.last_vk_, 0x41u);
    EXPECT_TRUE(exec.last_shift_);
    EXPECT_FALSE(exec.last_capsLock_);
    EXPECT_FALSE(exec.last_ctrl_);
    EXPECT_TRUE(exec.last_alt_);
    EXPECT_FALSE(exec.last_win_);
}

TEST(MacroFeature, MetadataIsPreEngine_Prio30_NoGates) {
    MockMacroExecutor exec;
    MacroFeature feature(exec);

    EXPECT_EQ(feature.FeatureStage(), Stage::PreEngine);
    EXPECT_EQ(feature.Priority(), 30);
    EXPECT_EQ(feature.Requires(), GateMask{0u});
}
