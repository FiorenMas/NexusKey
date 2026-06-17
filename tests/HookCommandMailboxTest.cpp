// HookCommandMailboxTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Phase 2a — single-writer composition state, mailbox infra. See
// docs/plans/2026-05-19-architecture-review-design.md §Phase 2.
//
// Locks in the data-structure contract that Phase 2b/c/d build on:
//   - atomic bit-OR coalescing (multiple posts of the same bit collapse)
//   - drain returns the OR of all pending bits and atomically clears them
//   - pendingFocus is a shared_ptr exchanged atomically with "later wins"
//     coalesce semantics (only the most recent focus state matters)
//   - wake fires exactly once per "empty → non-empty" edge, never on the
//     "still non-empty" transition (so a burst of posts collapses into a
//     single PostThreadMessage in the production wiring)
//   - drain ordering contract: wakePosted=false MUST happen BEFORE
//     bits.exchange(0), otherwise a producer racing with drain can have
//     its bit observed by drain but its wake suppressed — stranding the
//     work until the next unrelated keydown.
//
// Linux-portable: the mailbox itself owns no Win32 primitives; the wake
// function is injected at HookEngine::Start with a PostThreadMessage
// trampoline. Tests inject a counter / no-op.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "app/system/HookCommandMailbox.h"

namespace NextKey {
namespace {

using ::NextKey::HookCommand::kFocusChanged;
using ::NextKey::HookCommand::kConfigApply;
using ::NextKey::HookCommand::kTickPoll;
using ::NextKey::HookCommand::kToggleVN;

class HookCommandMailboxTest : public ::testing::Test {
protected:
    HookCommandMailbox mailbox_;
    std::atomic<int>   wakeCount_{0};

    void SetUp() override {
        mailbox_.SetWakeFn([this]{ wakeCount_.fetch_add(1, std::memory_order_relaxed); });
    }
};

// ──────────────────────────────────────────────────────────────────────────
// Empty state + single-bit basics
// ──────────────────────────────────────────────────────────────────────────
TEST_F(HookCommandMailboxTest, NewMailboxDrainReturnsZero) {
    EXPECT_EQ(mailbox_.DrainBits(), 0u);
    EXPECT_EQ(wakeCount_.load(), 0);
}

TEST_F(HookCommandMailboxTest, PostSingleBitDrainReturnsBit) {
    mailbox_.Post(kFocusChanged);
    EXPECT_EQ(mailbox_.DrainBits(), kFocusChanged);
}

TEST_F(HookCommandMailboxTest, DrainAfterDrainReturnsZero) {
    mailbox_.Post(kFocusChanged);
    (void)mailbox_.DrainBits();
    EXPECT_EQ(mailbox_.DrainBits(), 0u);
}

// ──────────────────────────────────────────────────────────────────────────
// Bit coalescing — multiple distinct + repeated posts collapse via fetch_or
// ──────────────────────────────────────────────────────────────────────────
TEST_F(HookCommandMailboxTest, PostMultipleBitsCoalesceInOR) {
    mailbox_.Post(kFocusChanged);
    mailbox_.Post(kConfigApply);
    mailbox_.Post(kTickPoll);
    EXPECT_EQ(mailbox_.DrainBits(), kFocusChanged | kConfigApply | kTickPoll);
}

TEST_F(HookCommandMailboxTest, PostSameBitMultipleTimesDrainReturnsOnce) {
    mailbox_.Post(kFocusChanged);
    mailbox_.Post(kFocusChanged);
    mailbox_.Post(kFocusChanged);
    EXPECT_EQ(mailbox_.DrainBits(), kFocusChanged)
        << "fetch_or must collapse repeated same-bit posts";
}

// ──────────────────────────────────────────────────────────────────────────
// pendingFocus — shared_ptr "later wins" coalesce
// ──────────────────────────────────────────────────────────────────────────
TEST_F(HookCommandMailboxTest, PendingFocusPostThenConsume) {
    auto cls = std::make_shared<const FocusClassification>();
    auto raw = cls.get();
    mailbox_.Post(kFocusChanged, std::move(cls));
    auto out = mailbox_.ConsumePendingFocus();
    ASSERT_TRUE(out);
    EXPECT_EQ(out.get(), raw);
}

TEST_F(HookCommandMailboxTest, PendingFocusCoalesceLaterWins) {
    auto cls1 = std::make_shared<const FocusClassification>();
    auto cls2 = std::make_shared<const FocusClassification>();
    auto raw2 = cls2.get();
    mailbox_.Post(kFocusChanged, std::move(cls1));
    mailbox_.Post(kFocusChanged, std::move(cls2));
    auto out = mailbox_.ConsumePendingFocus();
    ASSERT_TRUE(out);
    EXPECT_EQ(out.get(), raw2)
        << "second focus post must overwrite the first (only the latest "
           "focus state matters; the older one is stale once a newer one "
           "lands)";
}

TEST_F(HookCommandMailboxTest, PendingFocusConsumeOnceSecondReturnsNull) {
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    auto first = mailbox_.ConsumePendingFocus();
    auto second = mailbox_.ConsumePendingFocus();
    EXPECT_TRUE(first);
    EXPECT_FALSE(second) << "Consume is exchange(nullptr) — second call sees nothing";
}

TEST_F(HookCommandMailboxTest, PendingFocusOptionalOnNonFocusPost) {
    mailbox_.Post(kConfigApply);
    EXPECT_FALSE(mailbox_.ConsumePendingFocus())
        << "non-focus posts must not synthesise a pendingFocus";
}

// ──────────────────────────────────────────────────────────────────────────
// Wake edge semantics — fire once on empty → non-empty, re-arm after drain
// ──────────────────────────────────────────────────────────────────────────
TEST_F(HookCommandMailboxTest, WakeFiresOnFirstPost) {
    mailbox_.Post(kFocusChanged);
    EXPECT_EQ(wakeCount_.load(), 1);
}

TEST_F(HookCommandMailboxTest, WakeCoalesceMultiplePostsBeforeDrain) {
    mailbox_.Post(kFocusChanged);
    mailbox_.Post(kConfigApply);
    mailbox_.Post(kTickPoll);
    EXPECT_EQ(wakeCount_.load(), 1)
        << "wake must fire once per empty→non-empty edge — three posts "
           "with no drain in between is one edge";
}

TEST_F(HookCommandMailboxTest, WakeReArmsAfterDrain) {
    mailbox_.Post(kFocusChanged);
    (void)mailbox_.DrainBits();
    mailbox_.Post(kConfigApply);
    EXPECT_EQ(wakeCount_.load(), 2)
        << "drain must reset the wake latch so the next post fires wake again";
}

TEST_F(HookCommandMailboxTest, WakeFnUnsetPostStillRecordsBit) {
    HookCommandMailbox bare;  // no SetWakeFn
    bare.Post(kFocusChanged);
    EXPECT_EQ(bare.DrainBits(), kFocusChanged)
        << "wake fn is optional infrastructure; bit recording must work "
           "even before HookEngine wires the PostThreadMessage trampoline";
}

// ──────────────────────────────────────────────────────────────────────────
// Ordering contract — the most subtle invariant in the design doc.
//
// design §Critical ordering rule:
//   "wakePosted=false MUST happen BEFORE bits.exchange(0). Otherwise a
//    poster between exchange and wakePosted-clear sees wakePosted==true,
//    skips post, and its bit is stranded until the next keydown."
//
// We test the visible behavioural consequence: AFTER a drain, the
// IsWakePending() flag is false BEFORE bits are observed empty, so a
// concurrent post sees the cleared flag and fires wake.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(HookCommandMailboxTest, DrainClearsWakeLatchBeforeReturning) {
    mailbox_.Post(kFocusChanged);
    EXPECT_TRUE(mailbox_.IsWakePending())
        << "post sets the wake latch";
    (void)mailbox_.DrainBits();
    EXPECT_FALSE(mailbox_.IsWakePending())
        << "drain must clear wake latch — otherwise concurrent posts "
           "after drain returns get wake suppressed";
}

// ──────────────────────────────────────────────────────────────────────────
// ResetLatch — restart safety. A Post whose WM_APP_HOOK_COMMAND the pump
// exited before draining leaves wakePosted_ stuck true; without a reset the
// first Post after a restart would have its wake suppressed and strand the
// command until an unrelated keydown drains it.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(HookCommandMailboxTest, ResetLatchClearsBitsAndWakeLatchSoNextPostWakes) {
    int wakes = 0;
    mailbox_.SetWakeFn([&] { ++wakes; });

    mailbox_.Post(kFocusChanged);          // sets bits + latch, fires wake #1
    EXPECT_EQ(wakes, 1);
    EXPECT_TRUE(mailbox_.IsWakePending());
    EXPECT_NE(mailbox_.PeekBits(), 0u);

    mailbox_.ResetLatch();                 // simulate Stop() without a drain
    EXPECT_FALSE(mailbox_.IsWakePending());
    EXPECT_EQ(mailbox_.PeekBits(), 0u);

    mailbox_.Post(kConfigApply);           // first post of the "new session"
    EXPECT_EQ(wakes, 2)
        << "after ResetLatch the next Post must fire its wake (latch not stuck)";
}

// ──────────────────────────────────────────────────────────────────────────
// Concurrency stress — N producer threads pounding distinct bits while
// one drainer thread sweeps. After all producers finish, drainer must
// have observed at least one set instance of every producer's bit.
// (We don't count totals because fetch_or coalesces same-bit posts.)
// ──────────────────────────────────────────────────────────────────────────
TEST_F(HookCommandMailboxTest, StressMultipleProducersOneDrainerNoStrandedBits) {
    constexpr int kProducers       = 4;
    constexpr int kPostsPerProducer = 5000;
    static_assert(kProducers <= 4,
                  "test allocates one HookCommand bit per producer "
                  "(kFocusChanged/kConfigApply/kTickPoll/kToggleVN)");
    constexpr uint32_t kBits[4] = {kFocusChanged, kConfigApply, kTickPoll, kToggleVN};

    std::atomic<bool>     stop{false};
    std::atomic<uint32_t> bitsEverSeen{0};

    std::thread drainer([&]{
        while (!stop.load(std::memory_order_acquire) || mailbox_.PeekBits() != 0) {
            uint32_t b = mailbox_.DrainBits();
            if (b) bitsEverSeen.fetch_or(b, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p]{
            for (int i = 0; i < kPostsPerProducer; ++i) {
                mailbox_.Post(kBits[p]);
            }
        });
    }
    for (auto& t : producers) t.join();
    stop.store(true, std::memory_order_release);
    drainer.join();

    EXPECT_EQ(mailbox_.PeekBits(), 0u)
        << "no bits should remain after producers finished and drainer "
           "fully consumed";
    for (int p = 0; p < kProducers; ++p) {
        EXPECT_NE(bitsEverSeen.load() & kBits[p], 0u)
            << "producer " << p << "'s bit (0x" << std::hex << kBits[p]
            << ") was never seen by drainer — stranded post bug";
    }

    // Wake fired at least once. (Bound depends on coalescing — there could
    // be 1 wake if drain stayed behind, or many if drainer kept up. The
    // contract is "at least one wake per non-empty period", not a count.)
    EXPECT_GE(wakeCount_.load(), 1)
        << "drainer must have been woken at least once";
}

}  // namespace
}  // namespace NextKey
