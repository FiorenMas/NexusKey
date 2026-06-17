// ThreadOwnershipTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Phase 2d — single-writer thread-ownership invariants. See
// docs/plans/2026-05-19-architecture-review-design.md §Phase 2.
//
// Locks the data-structure contract that pairs with the in-source
// `assert(GetCurrentThreadId() == hookThreadId_)` lines added at the 8
// composition-state mutation entry points (ResetComposition,
// CommitComposition, ClearWordState, ReplaceComposition,
// ReplayCommittedChars, HandleAlphaKey, HandleBackspace,
// ApplyConfigOnHookThread). The thread-id asserts themselves are
// Win32-only (HookEngine.cpp); this suite tests the mailbox-side guard
// (DrainScope) that all those entry points are reachable from.
//
// The DrainScope RAII pattern:
//   - HookEngine::DrainHookCommands constructs a DrainScope at the top.
//   - IsDraining() returns true for the lifetime of the scope.
//   - Re-entering DrainHookCommands while a scope is alive trips an
//     assertion in Debug — catches the "drain handler calls back into
//     drain" bug at the earliest possible point.
//   - Phase 4's replay harness will use IsDraining() to assert at
//     forbidden-API entry points (nested PeekMessage, recursive Post,
//     etc.) in Debug builds.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "app/system/HookCommandMailbox.h"

namespace NextKey {
namespace {

TEST(MailboxDrainScope, NotDrainingByDefault) {
    HookCommandMailbox mb;
    EXPECT_FALSE(mb.IsDraining());
}

TEST(MailboxDrainScope, ScopeSetsFlagOnConstructionClearsOnDestruction) {
    HookCommandMailbox mb;
    {
        HookCommandMailbox::DrainScope scope(mb);
        EXPECT_TRUE(mb.IsDraining())
            << "drain scope must publish IsDraining() = true for the "
               "duration of the dispatch; production asserts (Debug "
               "only) read this at forbidden re-entry points";
    }
    EXPECT_FALSE(mb.IsDraining())
        << "scope dtor must restore IsDraining() = false; otherwise a "
           "subsequent legit drain would assert spuriously";
}

TEST(MailboxDrainScope, FlagFlipsAcrossRepeatedScopes) {
    HookCommandMailbox mb;
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(mb.IsDraining());
        {
            HookCommandMailbox::DrainScope scope(mb);
            EXPECT_TRUE(mb.IsDraining());
        }
        EXPECT_FALSE(mb.IsDraining());
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Drain handler invariant: a handler that calls Post() during dispatch
// MUST land its bit in the NEXT drain pass, not the current one. Combined
// with DrainScope, this is the canonical "no recursive drain" contract.
// (MidDrainPostDoesNotInfluenceCurrentPass in FocusInterleavingTest covers
// the bit-isolation half; here we add the IsDraining()-flag-visible half.)
// ──────────────────────────────────────────────────────────────────────────
TEST(MailboxDrainScope, HandlerSeesFlagTrueThroughout) {
    HookCommandMailbox mb;
    std::atomic<int>   observedDraining{-1};

    {
        HookCommandMailbox::DrainScope scope(mb);
        // Simulated dispatch — a handler reads IsDraining() to decide
        // whether it's allowed to call certain Win32 APIs (Phase 4
        // forbidden-API audit). The flag must be observable as true
        // for the entire scope, not just at scope entry/exit.
        observedDraining.store(mb.IsDraining() ? 1 : 0,
                               std::memory_order_release);
    }
    EXPECT_EQ(observedDraining.load(std::memory_order_acquire), 1);
}

// ──────────────────────────────────────────────────────────────────────────
// Cross-thread query semantics — IsDraining() reads atomic; producers on
// other threads can observe the flag without TSAN warnings. Verifying
// this locks the choice of atomic<bool> for the underlying storage.
// ──────────────────────────────────────────────────────────────────────────
TEST(MailboxDrainScope, OtherThreadObservesDrainScope) {
    HookCommandMailbox mb;
    std::atomic<bool> producerSawDraining{false};
    std::atomic<bool> startProbe{false};
    std::atomic<bool> stopProbe{false};

    std::thread probe([&]() {
        while (!startProbe.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stopProbe.load(std::memory_order_acquire)) {
            if (mb.IsDraining()) {
                producerSawDraining.store(true, std::memory_order_release);
                break;
            }
            std::this_thread::yield();
        }
    });

    {
        HookCommandMailbox::DrainScope scope(mb);
        startProbe.store(true, std::memory_order_release);
        // Give the probe thread a window to observe IsDraining() == true.
        // Use sleep_for instead of pure yield so the CI runner's scheduler
        // is guaranteed to give the probe wall-clock cycles even when the
        // host is heavily loaded (yield-only made this flake on Windows CI).
        for (int i = 0; i < 200 && !producerSawDraining.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    stopProbe.store(true, std::memory_order_release);
    probe.join();
    EXPECT_TRUE(producerSawDraining.load())
        << "IsDraining() must be visible to other threads via atomic load";
}

}  // namespace
}  // namespace NextKey
