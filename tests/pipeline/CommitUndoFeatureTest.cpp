// tests/pipeline/CommitUndoFeatureTest.cpp
//
// W3.5 — verify CommitUndoFeature is a thin wrapper that delegates the
// keystroke to an ICommitUndoExecutor and maps the CommitUndoOutcome to a
// Result + a flow-control intent (ConsumeKey / PassThrough / none). Also
// pins the (Stage, Priority, Requires) metadata and confirms the feature
// holds no per-call state.
#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "core/pipeline/CommitUndoFeature.h"
#include "core/pipeline/GateMask.h"
#include "core/pipeline/ICommitUndoExecutor.h"
#include "core/pipeline/ICompositionSession.h"
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

class MockCommitUndoExecutor final : public ICommitUndoExecutor {
public:
    CommitUndoOutcome HandleCommitUndo(std::uint16_t vkCode) override {
        last_vk_ = vkCode;
        ++calls_;
        return outcome_;
    }

    CommitUndoOutcome outcome_ = CommitUndoOutcome::Fallthrough;
    std::uint16_t     last_vk_ = 0;
    int               calls_ = 0;
};

class RecordingSink final : public IntentSink {
public:
    void Emit(Intent intent) override { intents_.push_back(std::move(intent)); }

    std::vector<Intent> intents_;
};

KeyContext makeStubCtx(const ICompositionSession& session, std::uint16_t vk = 0x41) {
    return KeyContext{
        .vk = vk, .keyChar = L'\0',
        .shift = false, .capsLock = false, .ctrl = false, .alt = false, .win = false,
        .session = &session, .reinjectVk = 0,
    };
}

}  // namespace

TEST(CommitUndoFeature, EatOutcomeEmitsConsumeKeyAndHandled) {
    MockCommitUndoExecutor exec;
    exec.outcome_ = CommitUndoOutcome::Eat;
    CommitUndoFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Handled);
    ASSERT_EQ(sink.intents_.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Intents::ConsumeKey>(sink.intents_[0]));
}

TEST(CommitUndoFeature, PassOutcomeEmitsPassThroughAndVeto) {
    MockCommitUndoExecutor exec;
    exec.outcome_ = CommitUndoOutcome::Pass;
    CommitUndoFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Veto);
    ASSERT_EQ(sink.intents_.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Intents::PassThrough>(sink.intents_[0]));
}

TEST(CommitUndoFeature, FallthroughOutcomeEmitsNothingAndPass) {
    MockCommitUndoExecutor exec;
    exec.outcome_ = CommitUndoOutcome::Fallthrough;
    CommitUndoFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session);
    RecordingSink sink;

    const Result r = feature.Try(ctx, sink);

    EXPECT_EQ(r, Result::Pass);
    EXPECT_TRUE(sink.intents_.empty());
}

TEST(CommitUndoFeature, PassesVkToExecutor) {
    MockCommitUndoExecutor exec;
    exec.outcome_ = CommitUndoOutcome::Fallthrough;
    CommitUndoFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeStubCtx(session, /*vk=*/0x42);
    RecordingSink sink;

    (void)feature.Try(ctx, sink);

    EXPECT_EQ(exec.calls_, 1);
    EXPECT_EQ(exec.last_vk_, 0x42u);
}

TEST(CommitUndoFeature, MetadataIsPreEngine_Prio20_NoGates) {
    MockCommitUndoExecutor exec;
    CommitUndoFeature feature(exec);

    EXPECT_EQ(feature.FeatureStage(), Stage::PreEngine);
    EXPECT_EQ(feature.Priority(), 20);
    EXPECT_EQ(feature.Requires(), GateMask{0u});
}

TEST(CommitUndoFeature, MultipleCallsAreStateless) {
    MockCommitUndoExecutor exec;
    CommitUndoFeature feature(exec);
    StubSession session(L"", L"", L"");
    RecordingSink sink;

    // First call: Eat with vk=0x10
    exec.outcome_ = CommitUndoOutcome::Eat;
    KeyContext ctx1 = makeStubCtx(session, /*vk=*/0x10);
    const Result r1 = feature.Try(ctx1, sink);
    EXPECT_EQ(r1, Result::Handled);
    EXPECT_EQ(exec.last_vk_, 0x10u);
    ASSERT_EQ(sink.intents_.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Intents::ConsumeKey>(sink.intents_[0]));

    // Second call: Pass with vk=0x20 — independent of first.
    exec.outcome_ = CommitUndoOutcome::Pass;
    KeyContext ctx2 = makeStubCtx(session, /*vk=*/0x20);
    const Result r2 = feature.Try(ctx2, sink);
    EXPECT_EQ(r2, Result::Veto);
    EXPECT_EQ(exec.last_vk_, 0x20u);
    EXPECT_EQ(exec.calls_, 2);
    ASSERT_EQ(sink.intents_.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<Intents::PassThrough>(sink.intents_[1]));
}
