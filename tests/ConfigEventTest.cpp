// VKey - ConfigEvent Tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include "core/config/ConfigEvent.h"
#include <thread>
#include <chrono>

using namespace NextKey;

class ConfigEventTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test event initialization
TEST_F(ConfigEventTest, Initialize) {
    ConfigEvent event;
    EXPECT_FALSE(event.IsValid());
    
    EXPECT_TRUE(event.Initialize());
    EXPECT_TRUE(event.IsValid());
}

// Test signal and wait
TEST_F(ConfigEventTest, SignalAndWait) {
    ConfigEvent event;
    ASSERT_TRUE(event.Initialize());
    
    // Event should not be signaled initially
    EXPECT_FALSE(event.Wait(0));
    
    // Signal the event
    event.Signal();
    
    // Now wait should return true (auto-reset event)
    EXPECT_TRUE(event.Wait(0));
    
    // After auto-reset, wait should return false again
    EXPECT_FALSE(event.Wait(0));
}

// Test timeout behavior
TEST_F(ConfigEventTest, WaitTimeout) {
    ConfigEvent event;
    ASSERT_TRUE(event.Initialize());
    
    auto start = std::chrono::steady_clock::now();
    
    // Wait with timeout - should return false after timeout
    EXPECT_FALSE(event.Wait(50));  // 50ms timeout
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    
    // Should have waited approximately 50ms (allow some tolerance)
    EXPECT_GE(ms, 40);
    EXPECT_LE(ms, 200);
}

// Test cross-instance signaling (same process)
TEST_F(ConfigEventTest, CrossInstanceSignaling) {
    ConfigEvent sender;
    ConfigEvent receiver;
    
    ASSERT_TRUE(sender.Initialize());
    ASSERT_TRUE(receiver.Initialize());
    
    // Signal from sender
    sender.Signal();
    
    // Receiver should see it
    EXPECT_TRUE(receiver.Wait(100));
}

// Test signal from background thread
TEST_F(ConfigEventTest, SignalFromThread) {
    ConfigEvent event;
    ASSERT_TRUE(event.Initialize());
    
    bool signaled = false;
    
    // Start thread that will signal after a delay
    std::thread signaler([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ConfigEvent threadEvent;
        threadEvent.Initialize();
        threadEvent.Signal();
    });
    
    // Wait for signal with timeout
    signaled = event.Wait(500);
    
    signaler.join();
    
    EXPECT_TRUE(signaled);
}
