// tests/pipeline/CoordinatorDispatchTest.cpp
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "core/pipeline/Coordinator.h"
#include "core/pipeline/IFeature.h"
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

TEST(CoordinatorDispatch, StagesRunInOrder_PreEngineThenEngineThenPostEngine) {
    std::vector<std::string> log;
    Coordinator coord;
    coord.Register(std::make_unique<TaggingFeature>("post", Stage::PostEngine, 10, Result::Pass, log));
    coord.Register(std::make_unique<TaggingFeature>("pre",  Stage::PreEngine,  10, Result::Pass, log));
    coord.Register(std::make_unique<TaggingFeature>("eng",  Stage::Engine,     10, Result::Pass, log));

    FakeSession session;
    NullSink sink;
    coord.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "pre");
    EXPECT_EQ(log[1], "eng");
    EXPECT_EQ(log[2], "post");
}

TEST(CoordinatorDispatch, WithinStageRunsLowerPriorityFirst) {
    std::vector<std::string> log;
    Coordinator coord;
    coord.Register(std::make_unique<TaggingFeature>("p20", Stage::PreEngine, 20, Result::Pass, log));
    coord.Register(std::make_unique<TaggingFeature>("p5",  Stage::PreEngine,  5, Result::Pass, log));
    coord.Register(std::make_unique<TaggingFeature>("p10", Stage::PreEngine, 10, Result::Pass, log));

    FakeSession session;
    NullSink sink;
    coord.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "p5");
    EXPECT_EQ(log[1], "p10");
    EXPECT_EQ(log[2], "p20");
}

TEST(CoordinatorDispatch, HandledStopsCurrentStageButRunsLaterStages) {
    std::vector<std::string> log;
    Coordinator coord;
    coord.Register(std::make_unique<TaggingFeature>("pre-first",  Stage::PreEngine, 10, Result::Handled, log));
    coord.Register(std::make_unique<TaggingFeature>("pre-second", Stage::PreEngine, 20, Result::Pass,    log));
    coord.Register(std::make_unique<TaggingFeature>("post",       Stage::PostEngine, 10, Result::Pass,   log));

    FakeSession session;
    NullSink sink;
    coord.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "pre-first");
    EXPECT_EQ(log[1], "post");
}

TEST(CoordinatorDispatch, VetoSkipsAllRemainingStages) {
    std::vector<std::string> log;
    Coordinator coord;
    coord.Register(std::make_unique<TaggingFeature>("pre",  Stage::PreEngine,  10, Result::Veto,    log));
    coord.Register(std::make_unique<TaggingFeature>("eng",  Stage::Engine,     10, Result::Pass,    log));
    coord.Register(std::make_unique<TaggingFeature>("post", Stage::PostEngine, 10, Result::Pass,    log));

    FakeSession session;
    NullSink sink;
    coord.HandleKey(makeCtx(session), sink);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "pre");
}
