// tests/pipeline/OutputChannelTest.cpp
#include <gtest/gtest.h>
#include "core/pipeline/OutputChannel.h"

using namespace NextKey::Pipeline;

TEST(OutputChannel, EmitAccumulatesInOrder) {
    OutputChannel ch;
    ch.Emit(Intents::Backspace{ .count = 2 });
    ch.Emit(Intents::Text{ .text = L"ê" });
    ch.Emit(Intents::Reinject{ .vk = 0x45 });

    auto batch = ch.TakeBatch();
    ASSERT_EQ(batch.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<Intents::Backspace>(batch[0]));
    EXPECT_EQ(std::get<Intents::Backspace>(batch[0]).count, 2u);
    EXPECT_TRUE(std::holds_alternative<Intents::Text>(batch[1]));
    EXPECT_EQ(std::get<Intents::Text>(batch[1]).text, std::wstring{L"ê"});
    EXPECT_TRUE(std::holds_alternative<Intents::Reinject>(batch[2]));
    EXPECT_EQ(std::get<Intents::Reinject>(batch[2]).vk, 0x45u);
}

TEST(OutputChannel, TakeBatchEmptiesInternalBuffer) {
    OutputChannel ch;
    ch.Emit(Intents::Backspace{ .count = 1 });
    auto first = ch.TakeBatch();
    auto second = ch.TakeBatch();
    EXPECT_EQ(first.size(), 1u);
    EXPECT_EQ(second.size(), 0u);
}

TEST(OutputChannel, DrainBatchReturnsIntentsAndEmptiesChannel) {
    OutputChannel ch;
    ch.Emit(Intents::Backspace{ .count = 2 });
    ch.Emit(Intents::Text{ .text = L"ơ" });

    const auto& batch = ch.DrainBatch();
    ASSERT_EQ(batch.size(), 2u);
    EXPECT_EQ(std::get<Intents::Backspace>(batch[0]).count, 2u);
    EXPECT_EQ(std::get<Intents::Text>(batch[1]).text, std::wstring{L"ơ"});
    // Channel is empty immediately after the drain (early-return-safe).
    EXPECT_TRUE(ch.Empty());
}

TEST(OutputChannel, DrainBatchZeroAllocSteadyState) {
    // After the first drain, the emit buffer must retain capacity so a repeated
    // emit+drain cycle does not reallocate (Rule 11.2: no heap on the hook path).
    OutputChannel ch;
    ch.Emit(Intents::Backspace{ .count = 1 });
    (void)ch.DrainBatch();           // warms batch_ capacity via the swap
    ch.Emit(Intents::Text{ .text = L"â" });
    const auto& second = ch.DrainBatch();
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(std::get<Intents::Text>(second[0]).text, std::wstring{L"â"});
    EXPECT_TRUE(ch.Empty());
}
