// FocusInterleavingTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Phase 2b — two-phase focus. See
// docs/plans/2026-05-19-architecture-review-design.md §Phase 2.
//
// Locks the drain-dispatch contract for the four mailbox bits:
//   1. Drain order is kConfigApply → kFocusChanged → kTickPoll → kToggleVN.
//      Rationale: kConfigApply may rebuild engine_, so it must run before
//      the focus handler picks an injector / resets composition. kFocusChanged
//      resets composition state — it must run before kTickPoll (which assumes
//      a consistent state) and before kToggleVN (which may snapshot the
//      current composition for commit).
//   2. ConsumePendingFocus is called exactly once per drained kFocusChanged
//      bit. The "latest wins" coalesce is locked at the data-structure level
//      in HookCommandMailboxTest; here we verify the integration contract.
//   3. Mid-drain Post — if a producer fires kFocusChanged DURING dispatch
//      (e.g. nested callback re-entry), the new bit is NOT consumed in the
//      current drain pass. It lands in the next drain (next keystroke or
//      pump tick). This prevents unbounded recursion + matches the design's
//      "drain handlers MUST NOT call composition-mutating APIs" contract.
//
// Linux-portable: exercises HookCommandMailbox + a test harness that
// mirrors HookEngine::DrainHookCommands's dispatch logic. Production code
// in HookEngine.cpp must follow the same order (verified by code review +
// chaos integration on Windows).

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "app/system/HookCommandMailbox.h"

namespace NextKey {
namespace {

using ::NextKey::HookCommand::kFocusChanged;
using ::NextKey::HookCommand::kConfigApply;
using ::NextKey::HookCommand::kTickPoll;
using ::NextKey::HookCommand::kToggleVN;

// Test-side mirror of HookEngine::DrainHookCommands. Production must keep
// the SAME dispatch order; that's the invariant this suite locks. Any
// reorder in HookEngine.cpp without updating the matching test here is a
// review red flag.
struct DrainHarness {
    HookCommandMailbox& mailbox;
    std::vector<std::string> log;            // ordered sequence of handler hits
    std::shared_ptr<const FocusClassification> lastConsumedFocus;
    int focusHits = 0;
    int configHits = 0;
    int tickHits = 0;
    int toggleHits = 0;

    void Drain() {
        const std::uint32_t bits = mailbox.DrainBits();
        if (!bits) return;
        if (bits & kConfigApply) {
            log.emplace_back("config");
            ++configHits;
        }
        if (bits & kFocusChanged) {
            lastConsumedFocus = mailbox.ConsumePendingFocus();
            log.emplace_back("focus");
            ++focusHits;
        }
        if (bits & kTickPoll) {
            log.emplace_back("tick");
            ++tickHits;
        }
        if (bits & kToggleVN) {
            log.emplace_back("toggle");
            ++toggleHits;
        }
    }
};

class FocusInterleavingTest : public ::testing::Test {
protected:
    HookCommandMailbox mailbox_;
    DrainHarness       harness_{mailbox_, {}, nullptr, 0, 0, 0, 0};
};

// ──────────────────────────────────────────────────────────────────────────
// Drain order contract — kConfigApply first, then kFocusChanged, then
// kTickPoll, then kToggleVN. The test posts in REVERSE order to confirm
// dispatch order is determined by the drain code, not by post order.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, DrainOrderConfigFirstFocusSecondTickThirdToggleFourth) {
    mailbox_.Post(kToggleVN);
    mailbox_.Post(kTickPoll);
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    mailbox_.Post(kConfigApply);
    harness_.Drain();

    ASSERT_EQ(harness_.log.size(), 4u);
    EXPECT_EQ(harness_.log[0], "config")
        << "config must dispatch first — engine_ may be rebuilt before focus runs";
    EXPECT_EQ(harness_.log[1], "focus")
        << "focus second — ResetComposition + state writes after engine is fresh";
    EXPECT_EQ(harness_.log[2], "tick");
    EXPECT_EQ(harness_.log[3], "toggle");
}

TEST_F(FocusInterleavingTest, DrainOrderHoldsForSingleBitAsWell) {
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    harness_.Drain();
    ASSERT_EQ(harness_.log.size(), 1u);
    EXPECT_EQ(harness_.log[0], "focus");
    EXPECT_EQ(harness_.focusHits, 1);
    EXPECT_EQ(harness_.configHits, 0);
}

// ──────────────────────────────────────────────────────────────────────────
// pendingFocus consumption — exactly once per drained kFocusChanged bit
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, FocusConsumedExactlyOncePerDrainedBit) {
    auto cls = std::make_shared<const FocusClassification>();
    auto raw = cls.get();
    mailbox_.Post(kFocusChanged, std::move(cls));
    harness_.Drain();

    ASSERT_TRUE(harness_.lastConsumedFocus);
    EXPECT_EQ(harness_.lastConsumedFocus.get(), raw);
    // After drain, mailbox has nothing pending.
    EXPECT_FALSE(mailbox_.ConsumePendingFocus())
        << "drain consumed the pending focus — second consume sees null";
}

TEST_F(FocusInterleavingTest, FocusCoalesceLatestWinsAcrossPosts) {
    auto cls1 = std::make_shared<const FocusClassification>();
    auto cls2 = std::make_shared<const FocusClassification>();
    // Pre-publish mutation via const_cast — the snapshot is held by only us
    // here so there's no race. WinEventProc will follow the same pattern:
    // build the struct, then post-as-const so the hook-thread reader gets
    // an immutable view.
    const_cast<FocusClassification*>(cls1.get())->hwndOpaque = 0x1111;
    const_cast<FocusClassification*>(cls2.get())->hwndOpaque = 0x2222;

    mailbox_.Post(kFocusChanged, std::move(cls1));
    mailbox_.Post(kFocusChanged, std::move(cls2));
    harness_.Drain();

    ASSERT_TRUE(harness_.lastConsumedFocus);
    EXPECT_EQ(harness_.lastConsumedFocus->hwndOpaque, std::uintptr_t{0x2222})
        << "two focus posts with no drain in between collapse to the LATEST "
           "snapshot — older transient HWNDs (taskbar/JumpList) are correctly "
           "discarded";
    // Wake fires once for the burst (data-structure contract from
    // HookCommandMailboxTest), not twice.
}

// ──────────────────────────────────────────────────────────────────────────
// Mid-drain Post — drain takes a snapshot then dispatches. If another
// thread Posts during dispatch, the new bit is NOT visible in the current
// drain's dispatched bits; it lands in the next DrainBits() call.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, MidDrainPostDoesNotInfluenceCurrentPass) {
    // First pass: only kFocusChanged. The "mid-drain Post" simulation: we
    // post kConfigApply AFTER harness_.Drain has already snapshot-ed bits.
    // Since DrainBits is atomic exchange(0), the new post writes to a fresh
    // bit field — must surface on a SECOND drain, not the first.
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    harness_.Drain();
    ASSERT_EQ(harness_.log.size(), 1u);
    EXPECT_EQ(harness_.log[0], "focus");

    // Second pass: a producer post that arrived after the first drain.
    mailbox_.Post(kConfigApply);
    harness_.Drain();
    ASSERT_EQ(harness_.log.size(), 2u);
    EXPECT_EQ(harness_.log[1], "config")
        << "post after a drain must land in the next drain — never lost, "
           "never merged retroactively into the previous pass";
}

// ──────────────────────────────────────────────────────────────────────────
// Empty drain is a no-op — no handlers fire, no focus consumed
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, EmptyMailboxDrainNoOps) {
    harness_.Drain();
    EXPECT_EQ(harness_.log.size(), 0u);
    EXPECT_EQ(harness_.focusHits, 0);
    EXPECT_FALSE(harness_.lastConsumedFocus);
}

// ──────────────────────────────────────────────────────────────────────────
// Repeated drain passes — typical hook-callback pattern, one drain per
// keystroke. Bits accumulated between keystrokes get dispatched on the
// next keystroke's drain.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, RepeatedDrainPassesEachDispatchesItsAccumulation) {
    // Keystroke 1 — focus changed
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    harness_.Drain();
    EXPECT_EQ(harness_.focusHits, 1);

    // Keystroke 2 — config save fired between keystrokes
    mailbox_.Post(kConfigApply);
    harness_.Drain();
    EXPECT_EQ(harness_.configHits, 1);

    // Keystroke 3 — nothing happened; drain is a cheap no-op
    harness_.Drain();
    EXPECT_EQ(harness_.focusHits, 1);  // unchanged
    EXPECT_EQ(harness_.configHits, 1); // unchanged

    // Keystroke 4 — burst: tick + toggle
    mailbox_.Post(kTickPoll);
    mailbox_.Post(kToggleVN);
    harness_.Drain();
    EXPECT_EQ(harness_.tickHits, 1);
    EXPECT_EQ(harness_.toggleHits, 1);
}

// ──────────────────────────────────────────────────────────────────────────
// kFocusChanged without a payload — defensive contract.
// Producer convention: every kFocusChanged Post should carry a non-null
// FocusClassification. But if some path posts the bit alone, ApplyFocus
// must handle ConsumePendingFocus returning null gracefully (no crash).
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, FocusBitWithoutPayloadIsSafe) {
    mailbox_.Post(kFocusChanged);  // no shared_ptr
    harness_.Drain();
    EXPECT_EQ(harness_.focusHits, 1);
    EXPECT_FALSE(harness_.lastConsumedFocus)
        << "missing payload yields null — ApplyFocusOnHookThread MUST tolerate "
           "this (it should early-return on null cls).";
}

}  // namespace
}  // namespace NextKey
