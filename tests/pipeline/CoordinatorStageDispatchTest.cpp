// tests/pipeline/CoordinatorStageDispatchTest.cpp
//
// W3.2 — Coordinator::HandleKeyAtStage(stage, ctx, sink) — verify per-stage
// dispatch runs only features at the requested stage, isolates Veto to that
// stage, and evaluates gates exactly once per invocation.
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "core/pipeline/Coordinator.h"
#include "core/pipeline/IFeature.h"
#include "core/pipeline/IGate.h"
#include "core/pipeline/ICompositionSession.h"

using namespace NextKey::Pipeline;

namespace {

class TaggingFeature final : public IFeature {
public:
    TaggingFeature(std::string tag, Stage s, int prio,
                   Result r, std::vector<std::string>& log)
        : tag_{std::move(tag)}, stage_{s}, prio_{prio}, result_{r}, log_{log} {}
    [[nodiscard]] Stage    FeatureStage() const noexcept override { return stage_; }
    [[nodiscard]] int      Priority()     const noexcept override { return prio_; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return 0u; }
    [[nodiscard]] Result   Try(const KeyContext&, IntentSink&) override {
        log_.push_back(tag_);
        return result_;
    }
private:
    std::string  tag_;
    Stage        stage_;
    int          prio_;
    Result       result_;
    std::vector<std::string>& log_;
};

// Gate that counts how many times IsRaised has been called.
class CountingGate final : public IGate {
public:
    CountingGate(GateId id, int& counter) : id_{id}, counter_{counter} {}
    [[nodiscard]] GateId Id() const noexcept override { return id_; }
    [[nodiscard]] bool   IsRaised(const KeyContext&) const noexcept override {
        ++counter_;
        return false;
    }
private:
    GateId id_;
    int&   counter_;
};

class FakeSession final : public ICompositionSession {
public:
    [[nodiscard]] std::wstring_view PreviousRendered() const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view EngineRendered()   const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view RawInput()         const noexcept override { return {}; }
};

class NullSink final : public IntentSink {
public:
    void Emit(Intent) override {}
};

KeyContext makeCtx(const ICompositionSession& session) {
    return KeyContext{
        .vk = 0x41, .keyChar = L'a',
        .shift = false, .capsLock = false, .ctrl = false, .alt = false, .win = false,
        .session = &session, .reinjectVk = 0,
    };
}

}  // namespace

TEST(CoordinatorStageDispatch, HandleKeyAtStage_PreEngine_runsOnlyPreEngineFeatures) {
    std::vector<std::string> log;
    Coordinator coord;
    coord.Register(std::make_unique<TaggingFeature>("pre",  Stage::PreEngine,  10, Result::Pass, log));
    coord.Register(std::make_unique<TaggingFeature>("post", Stage::PostEngine, 10, Result::Pass, log));

    FakeSession session;
    NullSink sink;
    coord.HandleKeyAtStage(Stage::PreEngine, makeCtx(session), sink);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "pre");
}

TEST(CoordinatorStageDispatch, HandleKeyAtStage_PostEngine_runsOnlyPostEngineFeatures) {
    std::vector<std::string> log;
    Coordinator coord;
    coord.Register(std::make_unique<TaggingFeature>("pre",  Stage::PreEngine,  10, Result::Pass, log));
    coord.Register(std::make_unique<TaggingFeature>("post", Stage::PostEngine, 10, Result::Pass, log));

    FakeSession session;
    NullSink sink;
    coord.HandleKeyAtStage(Stage::PostEngine, makeCtx(session), sink);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "post");
}

TEST(CoordinatorStageDispatch, HandleKeyAtStage_Veto_doesNotAffectLaterStageCalls) {
    std::vector<std::string> log;
    Coordinator coord;
    coord.Register(std::make_unique<TaggingFeature>("pre",  Stage::PreEngine,  10, Result::Veto, log));
    coord.Register(std::make_unique<TaggingFeature>("post", Stage::PostEngine, 10, Result::Pass, log));

    FakeSession session;
    NullSink sink;

    // First call — PreEngine vetoes within its own stage; PostEngine untouched.
    coord.HandleKeyAtStage(Stage::PreEngine, makeCtx(session), sink);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "pre");

    // Second call — PostEngine still runs, unaffected by previous Veto.
    coord.HandleKeyAtStage(Stage::PostEngine, makeCtx(session), sink);
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[1], "post");
}

TEST(CoordinatorStageDispatch, HandleKeyAtStage_GatesEvalOnce) {
    std::vector<std::string> log;
    int gateCallCount = 0;
    Coordinator coord;
    coord.RegisterGate(std::make_unique<CountingGate>(GateId::EnglishBias, gateCallCount));
    // Three features at PreEngine — gate must still evaluate only once.
    coord.Register(std::make_unique<TaggingFeature>("p1", Stage::PreEngine, 10, Result::Pass, log));
    coord.Register(std::make_unique<TaggingFeature>("p2", Stage::PreEngine, 20, Result::Pass, log));
    coord.Register(std::make_unique<TaggingFeature>("p3", Stage::PreEngine, 30, Result::Pass, log));

    FakeSession session;
    NullSink sink;
    coord.HandleKeyAtStage(Stage::PreEngine, makeCtx(session), sink);

    EXPECT_EQ(gateCallCount, 1);
    ASSERT_EQ(log.size(), 3u);
}
