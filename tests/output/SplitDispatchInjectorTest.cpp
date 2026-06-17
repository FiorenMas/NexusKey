// tests/output/SplitDispatchInjectorTest.cpp
//
// Unit tests for SplitDispatchInjector. Mock SendInput + Sleep via test
// seams (no GUI required); inspect captured INPUT events and Sleep
// delays.
//
// Spec: docs/plans/sprint-2-output-injector.md §5.1, plan Task 19.
#include "InjectorTestBase.h"
#include "app/output/SplitDispatchInjector.h"

namespace NextKey::Output::Test {

class SplitDispatchInjectorTest : public InjectorTestBase {};

TEST_F(SplitDispatchInjectorTest, ReplaceSplitsBackspacesThenSleepsThenChars) {
    SplitDispatchInjector inj(/*sleepMsBetweenBatches=*/6);
    EXPECT_TRUE(inj.Replace(2, L"vi"));

    // Two batches: BS×2 (4 events) + chars×2 (4 events) = 8 events captured.
    ASSERT_EQ(capturedInputs.size(), 8u);
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[1].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[0].ki.dwFlags & KEYEVENTF_KEYUP, 0u);  // BS down
    EXPECT_NE(capturedInputs[1].ki.dwFlags & KEYEVENTF_KEYUP, 0u);  // BS up
    EXPECT_EQ(capturedInputs[2].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[3].ki.wVk, VK_BACK);

    EXPECT_NE(capturedInputs[4].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[4].ki.wScan, L'v');
    EXPECT_NE(capturedInputs[6].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[6].ki.wScan, L'i');

    // Sleep called exactly once between the two batches.
    ASSERT_EQ(sleepDelays.size(), 1u);
    EXPECT_EQ(sleepDelays[0], 6u);
}

TEST_F(SplitDispatchInjectorTest, BsCountZeroSkipsSleepAndFirstSend) {
    SplitDispatchInjector inj(6);
    EXPECT_TRUE(inj.Replace(0, L"x"));
    EXPECT_EQ(sleepDelays.size(), 0u);
    ASSERT_EQ(capturedInputs.size(), 2u);  // 1 char × (down + up)
    EXPECT_NE(capturedInputs[0].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[0].ki.wScan, L'x');
}

TEST_F(SplitDispatchInjectorTest, TextEmptySkipsSecondSendAndSleep) {
    SplitDispatchInjector inj(6);
    EXPECT_TRUE(inj.Replace(2, L""));
    EXPECT_EQ(sleepDelays.size(), 0u);  // no Sleep when no chars to follow
    ASSERT_EQ(capturedInputs.size(), 4u);  // BS×2 only
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[2].ki.wVk, VK_BACK);
}

TEST_F(SplitDispatchInjectorTest, SleepMsRespectsConstructorParam) {
    SplitDispatchInjector inj(/*sleepMsBetweenBatches=*/5);
    EXPECT_TRUE(inj.Replace(1, L"a"));
    ASSERT_EQ(sleepDelays.size(), 1u);
    EXPECT_EQ(sleepDelays[0], 5u);
}

TEST_F(SplitDispatchInjectorTest, PartialFirstSendReturnsFalseAndStopsSecondBatch) {
    SplitDispatchInjector inj(6);
    sendInputReturnOverride = 1;  // only 1 of N events delivered on every call
    EXPECT_FALSE(inj.Replace(2, L"x"));
    // Second batch must NOT have been attempted after first failed:
    // no Sleep, no char events captured beyond the BS attempt.
    EXPECT_EQ(sleepDelays.size(), 0u);
    ASSERT_EQ(capturedInputs.size(), 4u);  // only the BS attempt
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
}

TEST_F(SplitDispatchInjectorTest, SendKeyEmitsDownAndUpWithMarker) {
    SplitDispatchInjector inj(6);
    inj.SendKey(VK_BACK);
    ASSERT_EQ(capturedInputs.size(), 2u);
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[0].ki.dwExtraInfo, Internal::kVKeyExtraInfo);
    EXPECT_EQ(capturedInputs[0].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
    EXPECT_NE(capturedInputs[1].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
}

TEST_F(SplitDispatchInjectorTest, BaitCharFiresOnChromiumElectronWhenBsPositive) {
    // WebView2 / Electron-on-Chromium hosts (Tauri/Dorion): need bait
    // prefix in batch 1 alongside backspaces. The bait + extra BS land
    // before the Sleep so the autocomplete suggest engine dismisses
    // before the real deletes hit.
    SplitDispatchInjector inj(/*sleepMsBetweenBatches=*/6,
                              /*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.Replace(1, L"x"));
    // Expected first batch: bait (down+up=2) + 2 BS down/up (= 4: 1 orig + 1 extra) = 6 events.
    // Expected second batch: 1 char down/up (= 2). Total 8 events captured.
    ASSERT_EQ(capturedInputs.size(), 8u);
    EXPECT_NE(capturedInputs[0].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[0].ki.wScan, 0x202F);
    EXPECT_EQ(capturedInputs[2].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[4].ki.wVk, VK_BACK);
    EXPECT_NE(capturedInputs[6].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[6].ki.wScan, L'x');
    // Sleep still fires once between batch 1 and batch 2.
    ASSERT_EQ(sleepDelays.size(), 1u);
    EXPECT_EQ(sleepDelays[0], 6u);
}

TEST_F(SplitDispatchInjectorTest, BaitCharSkippedWhenBsCountZero) {
    SplitDispatchInjector inj(6, /*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.Replace(0, L"y"));
    // No BS → no bait. Just the char batch.
    ASSERT_EQ(capturedInputs.size(), 2u);
    EXPECT_NE(capturedInputs[0].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[0].ki.wScan, L'y');
    EXPECT_EQ(sleepDelays.size(), 0u);
}

TEST_F(SplitDispatchInjectorTest, BaitCharFiresOnPureBackspaceByDefault) {
    // Mirror of Win32SendInputInjectorTest equivalent. WebView2 / Tauri /
    // Dorion inherit the Chromium inline-autocomplete swallow-BS quirk
    // — default behavior forces the bait so engine state stays in sync.
    SplitDispatchInjector inj(/*sleepMsBetweenBatches=*/6,
                              /*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.Replace(2, L""));
    // Expected: bait + 3 BS (1 extra to delete bait) × down+up = 8 events.
    ASSERT_EQ(capturedInputs.size(), 8u);
    EXPECT_NE(capturedInputs[0].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[0].ki.wScan, 0x202F);
    for (size_t i = 2; i < 8; ++i) {
        EXPECT_EQ(capturedInputs[i].ki.wVk, VK_BACK);
    }
    EXPECT_EQ(sleepDelays.size(), 0u);  // pure-BS → no second batch → no Sleep
}

TEST_F(SplitDispatchInjectorTest, BaitCharSkippedOnPureBackspaceWhenSuggestKeepCharsOn) {
    // Opt-in setting: BS only dismisses the popup, preserves typed chars.
    SplitDispatchInjector inj(/*sleepMsBetweenBatches=*/6,
                              /*needsBaitCharPrefix=*/true);
    inj.SetSuggestKeepChars(true);
    EXPECT_TRUE(inj.Replace(2, L""));
    // Expected: 2 BS × (down + up) = 4 events. No bait, no extra BS, no Sleep.
    ASSERT_EQ(capturedInputs.size(), 4u);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(capturedInputs[i].ki.wVk, VK_BACK);
    }
    EXPECT_EQ(sleepDelays.size(), 0u);
}

TEST_F(SplitDispatchInjectorTest, ReplaceNotifiesSynthCounterPerBatch) {
    // D5: the split dispatch fires Internal::TrackedSendInput TWICE
    // (BS batch then char batch separated by Sleep). Each call invokes
    // the callback so HookEngine's synthEventsPending_ tracks both
    // halves. Replace(2, "vi") = 4 BS events (batch 1) + 4 char events
    // (batch 2). Callback observes [+4, +4].
    SplitDispatchInjector inj(/*sleepMsBetweenBatches=*/6);
    EXPECT_TRUE(inj.Replace(2, L"vi"));
    ASSERT_EQ(synthCounterDeltas.size(), 2u);
    EXPECT_EQ(synthCounterDeltas[0], 4);
    EXPECT_EQ(synthCounterDeltas[1], 4);
}

TEST_F(SplitDispatchInjectorTest, PartialFirstBatchEmitsCompensatingNegativeDelta) {
    // First batch partial-send → callback emits +n then -(n-sent). Second
    // batch never attempted (Replace returns false on partial first).
    SplitDispatchInjector inj(6);
    sendInputReturnOverride = 1;  // 4 expected, 1 delivered
    EXPECT_FALSE(inj.Replace(2, L"x"));
    ASSERT_EQ(synthCounterDeltas.size(), 2u);
    EXPECT_EQ(synthCounterDeltas[0], 4);
    EXPECT_EQ(synthCounterDeltas[1], -3);
}

}  // namespace NextKey::Output::Test
