// tests/pipeline/BackwardEditFeatureTest.cpp
//
// W2.3 — verify BackwardEditFeature is a thin wrapper that delegates the
// engine-rendered text + reinjectVk to an IBackwardEditExecutor and reports
// Result::Handled. Also pins the (Stage, Priority, Requires) metadata.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "core/pipeline/BackwardEditFeature.h"
#include "core/pipeline/GateMask.h"
#include "core/pipeline/IBackwardEditExecutor.h"
#include "core/pipeline/ICompositionSession.h"
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

class MockExecutor final : public IBackwardEditExecutor {
public:
    void ExecuteReplace(std::wstring_view newText, std::uint16_t reinjectVk) override {
        last_text_.assign(newText);
        last_vk_ = reinjectVk;
        ++calls_;
    }

    std::wstring   last_text_;
    std::uint16_t  last_vk_ = 0;
    int            calls_ = 0;
};

class NoopSink final : public IntentSink {
public:
    void Emit(Intent /*intent*/) override {}
};

KeyContext makeCtx(const ICompositionSession& session, std::uint16_t reinjectVk = 0) {
    return KeyContext{
        .vk = 0x41, .keyChar = L'a',
        .shift = false, .capsLock = false, .ctrl = false, .alt = false, .win = false,
        .session = &session, .reinjectVk = reinjectVk,
    };
}

}  // namespace

TEST(BackwardEditFeature, DelegatesEngineRenderedToExecutor) {
    MockExecutor exec;
    BackwardEditFeature feature(exec);
    StubSession session(L"hie", L"hiê", L"hie");
    KeyContext ctx = makeCtx(session, /*reinjectVk=*/0);
    NoopSink sink;

    (void)feature.Try(ctx, sink);

    EXPECT_EQ(exec.calls_, 1);
    EXPECT_EQ(exec.last_text_, L"hiê");
    EXPECT_EQ(exec.last_vk_, 0u);
}

TEST(BackwardEditFeature, PassesReinjectVk) {
    MockExecutor exec;
    BackwardEditFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeCtx(session, /*reinjectVk=*/0x41);
    NoopSink sink;

    (void)feature.Try(ctx, sink);

    EXPECT_EQ(exec.calls_, 1);
    EXPECT_EQ(exec.last_vk_, 0x41u);
}

TEST(BackwardEditFeature, ReturnsHandled) {
    MockExecutor exec;
    BackwardEditFeature feature(exec);
    StubSession session(L"", L"", L"");
    KeyContext ctx = makeCtx(session);
    NoopSink sink;

    EXPECT_EQ(feature.Try(ctx, sink), Result::Handled);
}

TEST(BackwardEditFeature, MetadataIsPostEngine_Prio10_RequiresEnglishBias) {
    MockExecutor exec;
    BackwardEditFeature feature(exec);

    EXPECT_EQ(feature.FeatureStage(), Stage::PostEngine);
    EXPECT_EQ(feature.Priority(), 10);
    EXPECT_EQ(feature.Requires(), GateMaskFor(GateId::EnglishBias));
}
