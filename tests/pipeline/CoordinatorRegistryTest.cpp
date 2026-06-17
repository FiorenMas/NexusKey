// tests/pipeline/CoordinatorRegistryTest.cpp
#include <gtest/gtest.h>
#include "core/pipeline/Coordinator.h"
#include "core/pipeline/IFeature.h"

using namespace NextKey::Pipeline;

namespace {

class NoOpFeature final : public IFeature {
public:
    NoOpFeature(Stage s, int p) : stage_{s}, prio_{p} {}
    [[nodiscard]] Stage    FeatureStage() const noexcept override { return stage_; }
    [[nodiscard]] int      Priority()     const noexcept override { return prio_; }
    [[nodiscard]] GateMask Requires()     const noexcept override { return 0u; }
    [[nodiscard]] Result   Try(const KeyContext&, IntentSink&) override { return Result::Pass; }
private:
    Stage stage_;
    int   prio_;
};

}  // namespace

TEST(CoordinatorRegistry, EmptyRegistryReturnsZeroFeaturesAtEveryStage) {
    Coordinator coord;
    EXPECT_EQ(coord.FeatureCountAtStage(Stage::PreEngine),  0u);
    EXPECT_EQ(coord.FeatureCountAtStage(Stage::Engine),     0u);
    EXPECT_EQ(coord.FeatureCountAtStage(Stage::PostEngine), 0u);
}

TEST(CoordinatorRegistry, RegisterPutsFeatureInDeclaredStage) {
    Coordinator coord;
    auto f = std::make_unique<NoOpFeature>(Stage::Engine, 10);
    coord.Register(std::move(f));
    EXPECT_EQ(coord.FeatureCountAtStage(Stage::PreEngine),  0u);
    EXPECT_EQ(coord.FeatureCountAtStage(Stage::Engine),     1u);
    EXPECT_EQ(coord.FeatureCountAtStage(Stage::PostEngine), 0u);
}

TEST(CoordinatorRegistry, MultipleFeaturesAtSameStageKeepsAll) {
    Coordinator coord;
    coord.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 10));
    coord.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 20));
    coord.Register(std::make_unique<NoOpFeature>(Stage::PreEngine, 5));
    EXPECT_EQ(coord.FeatureCountAtStage(Stage::PreEngine), 3u);
}
