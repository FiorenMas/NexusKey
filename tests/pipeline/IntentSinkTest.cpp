// tests/pipeline/IntentSinkTest.cpp
#include <gtest/gtest.h>
#include <vector>
#include "core/pipeline/Intent.h"
#include "core/pipeline/IntentSink.h"

using namespace NextKey::Pipeline;

namespace {

class RecordingSink final : public IntentSink {
public:
    void Emit(Intent i) override { intents_.push_back(std::move(i)); }
    const std::vector<Intent>& Intents() const noexcept { return intents_; }
private:
    std::vector<Intent> intents_;
};

}  // namespace

TEST(IntentSink, RecordsEmittedIntentsInOrder) {
    RecordingSink sink;
    sink.Emit(Intents::Backspace{ .count = 2 });
    sink.Emit(Intents::Text{ .text = L"ê" });
    sink.Emit(Intents::Reinject{ .vk = 0x45 });

    ASSERT_EQ(sink.Intents().size(), 3u);
    EXPECT_TRUE(std::holds_alternative<Intents::Backspace>(sink.Intents()[0]));
    EXPECT_TRUE(std::holds_alternative<Intents::Text>(sink.Intents()[1]));
    EXPECT_TRUE(std::holds_alternative<Intents::Reinject>(sink.Intents()[2]));
}
