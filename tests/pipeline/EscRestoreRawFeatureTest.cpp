// tests/pipeline/EscRestoreRawFeatureTest.cpp
//
// W4a.2 — verify EscRestoreRawFeature is a thin wrapper that delegates the
// keystroke to an IEscRestoreRawExecutor and maps the EscRestoreOutcome to a
// Result + a flow-control intent (ConsumeKey / none). Also pins the
// (Stage, Priority, Requires) metadata.
#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "core/pipeline/EscRestoreRawFeature.h"
#include "core/pipeline/GateMask.h"
#include "core/pipeline/ICompositionSession.h"
#include "core/pipeline/IEscRestoreRawExecutor.h"
#include "core/pipeline/Intent.h"
#include "core/pipeline/IntentSink.h"
#include "core/pipeline/KeyContext.h"
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

class MockEscRestoreExecutor final : public IEscRestoreRawExecutor {
public:
    EscRestoreOutcome TryEscRestore(std::uint16_t vkCode,
                                    bool shift, bool ctrl, bool alt, bool win) override {
        last_vk_    = vkCode;
        last_shift_ = shift;
        last_ctrl_  = ctrl;
        last_alt_   = alt;
        last_win_   = win;
        ++calls_;
        return outcome_;
    }

    EscRestoreOutcome outcome_   = EscRestoreOutcome::Fallthrough;
    std::uint16_t     last_vk_   = 0;
    bool              last_shift_ = false;
    bool              last_ctrl_  = false;
    bool              last_alt_   = false;
    bool              last_win_   = false;
    int               calls_     = 0;
};

class RecordingSink final : public IntentSink {
public:
    void Emit(Intent intent) override { intents_.push_back(std::move(intent)); }

    std::vector<Intent> intents_;
};

KeyContext makeStubCtx(const ICompositionSession& session,
                       std::uint16_t vk = 0x1B,
                       bool shift = false, bool ctrl = false,
                       bool alt = false, bool win = false) {
    return KeyContext{
        .vk = vk, .keyChar = L'\0',
        .shift = shift, .capsLock = false, .ctrl = ctrl, .alt = alt, .win = win,
        .session = &session, .reinjectVk = 0,
    };
}

}  // namespace

TEST(EscRestoreRawFeature, EatOutcomeEmitsConsumeKeyAndHandled) {
    MockEscRestoreExecutor exec;
    exec.outcome_ = EscRestoreOutcome::Eat;
    EscRestoreRawFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Handled);
    ASSERT_EQ(sink.intents_.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Intents::ConsumeKey>(sink.intents_[0]));
}

TEST(EscRestoreRawFeature, FallthroughOutcomeEmitsNothingAndPass) {
    MockEscRestoreExecutor exec;
    exec.outcome_ = EscRestoreOutcome::Fallthrough;
    EscRestoreRawFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Pass);
    EXPECT_TRUE(sink.intents_.empty());
}

TEST(EscRestoreRawFeature, PassesVkToExecutor) {
    MockEscRestoreExecutor exec;
    exec.outcome_ = EscRestoreOutcome::Fallthrough;
    EscRestoreRawFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session, /*vk=*/0x1B);  // VK_ESCAPE
    RecordingSink sink;

    (void)feature.Try(ctx, sink);

    EXPECT_EQ(exec.calls_, 1);
    EXPECT_EQ(exec.last_vk_, 0x1Bu);
}

TEST(EscRestoreRawFeature, PassesModsToExecutor) {
    MockEscRestoreExecutor exec;
    exec.outcome_ = EscRestoreOutcome::Fallthrough;
    EscRestoreRawFeature feature(exec);
    StubSession session(L"", L"", L"");
    // shift=false, ctrl=true, alt=true, win=false
    KeyContext ctx = makeStubCtx(session, /*vk=*/0x1B,
                                 /*shift=*/false, /*ctrl=*/true,
                                 /*alt=*/true, /*win=*/false);
    RecordingSink sink;

    (void)feature.Try(ctx, sink);

    EXPECT_EQ(exec.calls_, 1);
    EXPECT_FALSE(exec.last_shift_);
    EXPECT_TRUE(exec.last_ctrl_);
    EXPECT_TRUE(exec.last_alt_);
    EXPECT_FALSE(exec.last_win_);
}

TEST(EscRestoreRawFeature, MetadataIsPreEngine_Prio40_NoGates) {
    MockEscRestoreExecutor exec;
    EscRestoreRawFeature feature(exec);

    EXPECT_EQ(feature.FeatureStage(), Stage::PreEngine);
    EXPECT_EQ(feature.Priority(), 40);
    EXPECT_EQ(feature.Requires(), GateMask{0u});
}
