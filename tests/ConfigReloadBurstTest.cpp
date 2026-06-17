// ConfigReloadBurstTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Phase 4 — replay harness for concurrency invariants
// (docs/plans/2026-05-19-architecture-review-design.md §Phase 4).
//
// ConfigSnapshotTest.cpp covers the basic RCU contract (default-empty,
// store/load round-trip, last-store-wins, 4×reader+1×writer cycle).
// This suite ratchets it up to the failure modes Phase 3 specifically
// has to survive:
//
//   1. In-flight composition stability — a hook keystroke loads the
//      snapshot once and dereferences several fields across many lines
//      of code (RunTopGuards → HandlePreDispatch → DispatchKeyAction →
//      TryExpandMacro). A worker publish that lands mid-keystroke must
//      NOT mutate the reader's view; every field read by that keystroke
//      must come from the same publish (no cross-snapshot blend).
//
//   2. Burst republish + heavy read load — Settings-mash scenarios bump
//      configGeneration 50+ times in rapid succession; the worker tick
//      drains them serially but each drain runs a fresh
//      `RebuildSnapshotFromToml`. Stress that 100s of publishes against
//      4 concurrent readers don't tear, leak, or corrupt.
//
//   3. Old-snapshot lifetime — readers holding a stale `shared_ptr` keep
//      the object alive until they drop it; the use_count drops to zero
//      exactly when expected, and `unique()` / refcount semantics match
//      the contract HookEngine's hot path relies on.
//
//   4. Generation skew across releases — reader can hold gen=1 across
//      arbitrary writer cycles (gen=2,3,...,N) and still read internally
//      consistent fields keyed on gen=1.
//
// Linux-portable (atomic shared_ptr only; no Win32, no HookEngine).
// HookEngine integration is verified separately via Windows chaos.

#include <gtest/gtest.h>

#include "core/config/ConfigSnapshot.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace NextKey {
namespace {

// ──────────────────────────────────────────────────────────────────────────
// Helper: build a synthetic snapshot whose contents are derivable from the
// generation alone. This lets tests verify "every field I read agrees with
// my snapshot's generation" — the integrity invariant that catches torn
// publication if the atomic<shared_ptr<>> ever degraded.
// ──────────────────────────────────────────────────────────────────────────
std::shared_ptr<const ConfigSnapshot> MakeGenSnapshot(std::uint32_t gen) {
    std::unordered_map<std::wstring, std::wstring> macros = {
        {L"gen" + std::to_wstring(gen), L"value" + std::to_wstring(gen)},
        {L"shared",                     L"v" + std::to_wstring(gen)},
        {L"with space",                 L"multi gen" + std::to_wstring(gen)},
    };
    std::unordered_set<std::wstring> excluded = {
        L"excl" + std::to_wstring(gen) + L".exe"
    };
    std::unordered_set<std::wstring> tsf = {
        L"tsf" + std::to_wstring(gen) + L".exe"
    };
    std::unordered_map<std::wstring, CodeTable> enc = {
        {L"app" + std::to_wstring(gen) + L".exe",
         static_cast<CodeTable>(gen % 5)}
    };
    std::unordered_map<std::wstring, InputMethod> im = {
        {L"app" + std::to_wstring(gen) + L".exe",
         static_cast<InputMethod>(gen % 5)}
    };
    // appSendMethodOverrides — same gen-derived shape, codes parity-toggled
    // so the integrity check below can pin the value to its source generation.
    std::unordered_map<std::wstring, std::int8_t> send = {
        {L"app" + std::to_wstring(gen) + L".exe",
         static_cast<std::int8_t>(gen % 2)}
    };
    return std::make_shared<const ConfigSnapshot>(ConfigSnapshot::Build(
        std::move(macros), std::move(excluded), /*forcedVn*/ {}, std::move(tsf),
        std::move(enc), std::move(im), std::move(send), gen));
}

// Verify every field of a snapshot agrees with its `generation`.
// Returns true if internally consistent; false if any field was authored
// by a different generation (the failure signature of torn publication).
bool SnapshotInternallyConsistent(const ConfigSnapshot& s) {
    const auto g = s.generation;
    const auto gs = std::to_wstring(g);
    if (s.macroTable.count(L"gen" + gs) != 1) return false;
    if (s.macroTable.at(L"shared") != L"v" + gs) return false;
    if (s.excludedAppSet.count(L"excl" + gs + L".exe") != 1) return false;
    if (s.tsfAppSet.count(L"tsf" + gs + L".exe") != 1) return false;
    auto encIt = s.appEncodingOverrides.find(L"app" + gs + L".exe");
    if (encIt == s.appEncodingOverrides.end()) return false;
    if (encIt->second != static_cast<CodeTable>(g % 5)) return false;
    auto imIt = s.appInputMethodOverrides.find(L"app" + gs + L".exe");
    if (imIt == s.appInputMethodOverrides.end()) return false;
    if (imIt->second != static_cast<InputMethod>(g % 5)) return false;
    auto sendIt = s.appSendMethodOverrides.find(L"app" + gs + L".exe");
    if (sendIt == s.appSendMethodOverrides.end()) return false;
    if (sendIt->second != static_cast<std::int8_t>(g % 2)) return false;
    return true;
}

// ──────────────────────────────────────────────────────────────────────────
// 1. In-flight "composition" stability.
//
// A simulated keystroke loads the snapshot once and reads ~6 fields with
// short pauses between, modelling the multi-step hot path. A writer
// publishes a fresh snapshot midway. The reader must see ONE generation's
// data across all reads — never a mix.
// ──────────────────────────────────────────────────────────────────────────
TEST(ConfigReloadBurst, InFlightCompositionSeesStableSnapshot) {
    std::atomic<std::shared_ptr<const ConfigSnapshot>> field;
    field.store(MakeGenSnapshot(1), std::memory_order_release);

    std::atomic<bool> readerStarted{false};
    std::atomic<bool> writerPublished{false};
    std::atomic<bool> readerInconsistent{false};
    std::atomic<std::uint32_t> readerObservedGen{0};

    std::thread reader([&] {
        // Step 1: load snapshot (mimics hook hot path's single load
        // per keystroke).
        auto snap = field.load(std::memory_order_acquire);
        ASSERT_NE(snap, nullptr);
        readerObservedGen.store(snap->generation, std::memory_order_release);
        readerStarted.store(true, std::memory_order_release);

        // Step 2: read several fields with deliberate pauses, simulating
        // the cost of CPU work between snapshot field accesses on the
        // hook hot path (engine push, code-table lookup, macro probe).
        for (int i = 0; i < 6; ++i) {
            if (!SnapshotInternallyConsistent(*snap)) {
                readerInconsistent.store(true, std::memory_order_release);
                return;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    // Writer races to publish gen=2 while the reader is still holding gen=1.
    while (!readerStarted.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    field.store(MakeGenSnapshot(2), std::memory_order_release);
    writerPublished.store(true, std::memory_order_release);

    reader.join();
    EXPECT_EQ(readerObservedGen.load(), 1u)
        << "reader captured generation must be exactly the one published "
           "before its load — RCU pin semantics";
    EXPECT_FALSE(readerInconsistent.load())
        << "every field read across the simulated keystroke must come "
           "from the captured snapshot; a mismatch implies the writer's "
           "publish reached the reader mid-read (torn / shared mutation)";
    EXPECT_TRUE(writerPublished.load());

    // After the reader exits, a fresh load sees the newer snapshot.
    auto post = field.load(std::memory_order_acquire);
    EXPECT_EQ(post->generation, 2u);
    EXPECT_TRUE(SnapshotInternallyConsistent(*post));
}

// ──────────────────────────────────────────────────────────────────────────
// 2. Burst republish under heavy read load.
//
// 4 readers continuously load + verify; writer cycles 500 generations
// back-to-back. No reader observes inconsistency; final load sees the
// last publish.
// ──────────────────────────────────────────────────────────────────────────
TEST(ConfigReloadBurst, BurstRepublishUnderHeavyReadLoad) {
    std::atomic<std::shared_ptr<const ConfigSnapshot>> field;
    field.store(MakeGenSnapshot(1), std::memory_order_release);

    constexpr int kCycles = 500;
    constexpr int kReaders = 4;
    std::atomic<bool> stop{false};
    std::atomic<int> torn{0};
    std::atomic<std::uint64_t> loads{0};

    std::vector<std::thread> readers;
    for (int r = 0; r < kReaders; ++r) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_acquire)) {
                auto snap = field.load(std::memory_order_acquire);
                if (!snap) { ++torn; continue; }
                if (!SnapshotInternallyConsistent(*snap)) ++torn;
                ++loads;
            }
        });
    }

    // Single writer (matches HookEngine's RebuildSnapshotFromToml
    // single-producer contract — worker thread).
    for (std::uint32_t g = 2; g <= kCycles + 1; ++g) {
        field.store(MakeGenSnapshot(g), std::memory_order_release);
    }
    stop.store(true, std::memory_order_release);
    for (auto& t : readers) t.join();

    EXPECT_EQ(torn.load(), 0)
        << "no reader should observe a snapshot whose fields disagree "
           "with its own generation — RCU's whole point";
    EXPECT_GT(loads.load(), 0u)
        << "readers must have observed at least one load each — sanity";
    auto final = field.load(std::memory_order_acquire);
    ASSERT_NE(final, nullptr);
    EXPECT_EQ(final->generation, static_cast<std::uint32_t>(kCycles + 1));
}

// ──────────────────────────────────────────────────────────────────────────
// 3. Old-snapshot lifetime: refcount drops to zero only after the last
// holder releases. Locks the GC contract HookEngine implicitly relies on
// when shared_ptr is the chosen RCU primitive.
// ──────────────────────────────────────────────────────────────────────────
TEST(ConfigReloadBurst, OldSnapshotReleasedAfterReadersDrop) {
    std::atomic<std::shared_ptr<const ConfigSnapshot>> field;
    auto snap1 = MakeGenSnapshot(1);
    std::weak_ptr<const ConfigSnapshot> weak1 = snap1;
    field.store(snap1, std::memory_order_release);

    // Two readers grab handles to gen=1.
    auto r1 = field.load(std::memory_order_acquire);
    auto r2 = field.load(std::memory_order_acquire);
    snap1.reset();  // writer drops its local handle

    // Publish gen=2. weak1 is still alive because r1, r2 hold strong refs.
    field.store(MakeGenSnapshot(2), std::memory_order_release);
    EXPECT_FALSE(weak1.expired())
        << "gen=1 must outlive writer's drop while readers hold strong refs";

    r1.reset();
    EXPECT_FALSE(weak1.expired())
        << "one reader released; the other still holds gen=1";

    r2.reset();
    EXPECT_TRUE(weak1.expired())
        << "all readers released; gen=1 must now be reclaimable";
}

// ──────────────────────────────────────────────────────────────────────────
// 4. Generation skew — a long-lived reader holds gen=1 while the writer
// cycles through gen=2…N. Reader's view never mutates; final reload sees
// the new value.
// ──────────────────────────────────────────────────────────────────────────
TEST(ConfigReloadBurst, ReaderHoldsSnapshotPastManyWriterCycles) {
    std::atomic<std::shared_ptr<const ConfigSnapshot>> field;
    field.store(MakeGenSnapshot(1), std::memory_order_release);

    // Reader captures gen=1 and holds it.
    auto held = field.load(std::memory_order_acquire);
    ASSERT_NE(held, nullptr);
    EXPECT_EQ(held->generation, 1u);

    // Writer cycles aggressively.
    for (std::uint32_t g = 2; g <= 100; ++g) {
        field.store(MakeGenSnapshot(g), std::memory_order_release);
    }

    // Reader's view is unchanged.
    EXPECT_EQ(held->generation, 1u);
    EXPECT_TRUE(SnapshotInternallyConsistent(*held));
    EXPECT_EQ(held->excludedAppSet.count(L"excl1.exe"), 1u);
    EXPECT_EQ(held->macroTable.at(L"shared"), L"v1");

    // Fresh load picks up the latest.
    auto fresh = field.load(std::memory_order_acquire);
    EXPECT_EQ(fresh->generation, 100u);
    EXPECT_TRUE(SnapshotInternallyConsistent(*fresh));
}

// ──────────────────────────────────────────────────────────────────────────
// 5. Multi-reader long-hold + writer burst — every reader's captured
// snapshot stays consistent for the full hold duration regardless of
// concurrent publish pressure. All eight readers capture the snapshot
// BEFORE the writer's burst starts, so they all pin the same gen=1; the
// test verifies that gen survives intact across 199 republishes. The
// "different generations" variant (staggered reader launch so each pins
// a different gen) is a TODO follow-up — current pattern catches the
// dominant failure mode (mid-publish corruption of any pinned view).
// ──────────────────────────────────────────────────────────────────────────
TEST(ConfigReloadBurst, EightReadersHoldSameSnapshotStably) {
    std::atomic<std::shared_ptr<const ConfigSnapshot>> field;
    field.store(MakeGenSnapshot(1), std::memory_order_release);

    constexpr int kReaders = 8;
    std::atomic<int> torn{0};
    std::atomic<int> readersReady{0};
    std::atomic<bool> startBurst{false};

    std::vector<std::thread> readers;
    readers.reserve(kReaders);
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            auto snap = field.load(std::memory_order_acquire);
            readersReady.fetch_add(1, std::memory_order_acq_rel);
            // Wait for writer to start its burst — guarantees the reader
            // is actually holding an old snapshot when the writer races.
            while (!startBurst.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int k = 0; k < 50; ++k) {
                if (!SnapshotInternallyConsistent(*snap)) {
                    torn.fetch_add(1, std::memory_order_acq_rel);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    while (readersReady.load(std::memory_order_acquire) < kReaders) {
        std::this_thread::yield();
    }
    startBurst.store(true, std::memory_order_release);
    for (std::uint32_t g = 2; g <= 200; ++g) {
        field.store(MakeGenSnapshot(g), std::memory_order_release);
    }
    for (auto& t : readers) t.join();

    EXPECT_EQ(torn.load(), 0)
        << kReaders << " readers each held a snapshot for 50 simulated "
           "hot-path reads; none must have seen mid-publish corruption";
}

}  // namespace
}  // namespace NextKey
