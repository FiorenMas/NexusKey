// TypingConfigRCUTests.cpp
//
// Sprint 1 D6: regression test for HookEngine::config_ migration to
// std::atomic<std::shared_ptr<const TypingConfig>> (Rule #11.3 — RCU
// shared_ptr pattern for hook-thread-safe complex-struct reads without
// stateMutex_).
//
// HookEngine.cpp is Win32-only and not linked into the cross-platform
// VKeyTests target, so this file does not import HookEngine.h directly.
// It exercises the std::atomic<std::shared_ptr<T>> publish/observe semantics
// with TypingConfig as the value type — the same pattern HookEngine adopts
// in D6.
//
// Three properties under test:
//   1. Compile-time: std::atomic<std::shared_ptr<TypingConfig>> exists and
//      compiles (C++20 P0718). MSVC 16.11+ and GCC 12+ are required.
//   2. Round-trip: store + load returns a shared_ptr pointing at the same
//      object with identical field values.
//   3. Concurrent reader during writer swap: a reader holding an old
//      shared_ptr can still safely access its fields after the writer has
//      published a new shared_ptr — no use-after-free, no torn read of
//      individual fields, and the reader's view is internally consistent
//      (every field is from the same published config, never a mix).
//
// Behavioral verification of the actual HookEngine config_ migration is done
// via the chaos.toml + sustained.toml corpora on Windows post-build (D5.2
// anchor: docs/baselines/perf-baseline-d5.2-atomic-rest-{chaos,sustained}.md;
// D6 must show no new regression).

#include <gtest/gtest.h>

#include "core/config/TypingConfig.h"

#include <atomic>
#include <memory>
#include <thread>

namespace NextKey {
namespace {

// Compile-time: HookEngine::config_ relies on std::atomic<std::shared_ptr<T>>
// from C++20 P0718. Older toolchains fall back to the deprecated free-function
// std::atomic_load / std::atomic_store on raw shared_ptr, but the migration
// targets the modern API for clarity and lock-free guarantees where available.
//
// We don't static_assert is_always_lock_free here — std::atomic<shared_ptr<T>>
// is typically NOT lock-free (it manages a control block + pointer atomically,
// usually via a small embedded lock). That's acceptable for the writer side
// (rare, main-thread) and the per-keystroke read cost is still single-digit
// nanoseconds on modern hardware. The contention model is fundamentally
// different from a mutex: writer never blocks reader, reader never blocks
// writer — only readers contend with each other on the internal lock briefly.
static_assert(std::is_same_v<
    decltype(std::declval<std::atomic<std::shared_ptr<const TypingConfig>>&>().load()),
    std::shared_ptr<const TypingConfig>>,
    "Sprint 1 D6: HookEngine::config_ requires std::atomic<std::shared_ptr<const TypingConfig>> "
    "(C++20 P0718). Toolchain must be MSVC 16.11+ or GCC 12+.");

// Round-trip: store + load returns a shared_ptr to an object with identical
// field values. The simplest correctness check.
TEST(TypingConfigRCU, StoreLoadRoundTrip) {
    TypingConfig src{};
    src.inputMethod = InputMethod::VNI;
    src.codeTable = CodeTable::TCVN3;
    src.macroEnabled = true;
    src.macroTriggerSpace = false;
    src.macroTriggerEnter = true;
    src.spellExclusions = {L"hđ", L"đp"};

    std::atomic<std::shared_ptr<const TypingConfig>> field;
    field.store(std::make_shared<const TypingConfig>(src), std::memory_order_release);

    auto loaded = field.load(std::memory_order_acquire);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->inputMethod, InputMethod::VNI);
    EXPECT_EQ(loaded->codeTable, CodeTable::TCVN3);
    EXPECT_TRUE(loaded->macroEnabled);
    EXPECT_FALSE(loaded->macroTriggerSpace);
    EXPECT_TRUE(loaded->macroTriggerEnter);
    ASSERT_EQ(loaded->spellExclusions.size(), 2u);
    EXPECT_EQ(loaded->spellExclusions[0], L"hđ");
    EXPECT_EQ(loaded->spellExclusions[1], L"đp");
}

// Reader holds an old shared_ptr after the writer publishes a new one.
// The old object must remain alive (shared_ptr ref-count keeps it) and the
// reader's loaded fields must remain readable — no use-after-free, no
// dangling vector pointer, no torn struct.
//
// Mirrors the production scenario: hook thread loads config_ at the start of
// IsMacroTrigger, then main thread (ReloadFromToml) replaces config_ via
// store. Hook reader's loaded shared_ptr keeps the old config alive for the
// rest of its function call.
TEST(TypingConfigRCU, OldConfigKeptAliveByReader) {
    TypingConfig oldCfg{};
    oldCfg.macroEnabled = true;
    oldCfg.spellExclusions = {L"old1", L"old2"};

    TypingConfig newCfg{};
    newCfg.macroEnabled = false;
    newCfg.spellExclusions = {L"new1"};

    std::atomic<std::shared_ptr<const TypingConfig>> field;
    field.store(std::make_shared<const TypingConfig>(oldCfg), std::memory_order_release);

    // Reader snapshot
    auto reader = field.load(std::memory_order_acquire);
    ASSERT_NE(reader, nullptr);
    EXPECT_TRUE(reader->macroEnabled);

    // Writer publishes new config — overwrites the atomic, but the old
    // shared_ptr's refcount is still 2 (atomic field + reader).
    field.store(std::make_shared<const TypingConfig>(newCfg), std::memory_order_release);

    // The writer's store dropped the atomic's reference to oldCfg. The reader
    // still holds it. Old fields must still be accessible.
    EXPECT_TRUE(reader->macroEnabled);  // old value
    ASSERT_EQ(reader->spellExclusions.size(), 2u);
    EXPECT_EQ(reader->spellExclusions[0], L"old1");
    EXPECT_EQ(reader->spellExclusions[1], L"old2");

    // A fresh load returns the new config.
    auto fresh = field.load(std::memory_order_acquire);
    ASSERT_NE(fresh, nullptr);
    EXPECT_FALSE(fresh->macroEnabled);
    ASSERT_EQ(fresh->spellExclusions.size(), 1u);
    EXPECT_EQ(fresh->spellExclusions[0], L"new1");

    // Reader still holds the old config — pointers must differ.
    EXPECT_NE(reader.get(), fresh.get());
}

// Concurrent reader during writer swap. Writer thread cycles between two
// distinct configs in tight succession; reader thread loads + reads every
// macroTrigger* field repeatedly. Each loaded snapshot must be internally
// consistent — every field comes from the same config, never a half-updated
// mix. This is the property mutex protects, and that std::atomic<shared_ptr>
// gives us cheaper.
//
// We encode "internal consistency" as: each of the two published configs has
// a distinguishable signature across multiple fields. If a reader sees field
// X from cfgA but field Y from cfgB, the signature check fails — that would
// be the failure mode of plain assignment without atomic publication.
TEST(TypingConfigRCU, ConcurrentReaderInternallyConsistent) {
    constexpr int kReaderIterations = 50000;

    TypingConfig cfgA{};
    cfgA.macroTriggerSpace = true;
    cfgA.macroTriggerEnter = true;
    cfgA.macroTriggerTab = true;
    cfgA.macroTriggerDir = true;

    TypingConfig cfgB{};
    cfgB.macroTriggerSpace = false;
    cfgB.macroTriggerEnter = false;
    cfgB.macroTriggerTab = false;
    cfgB.macroTriggerDir = false;

    auto sharedA = std::make_shared<const TypingConfig>(cfgA);
    auto sharedB = std::make_shared<const TypingConfig>(cfgB);

    std::atomic<std::shared_ptr<const TypingConfig>> field;
    field.store(sharedA, std::memory_order_release);

    std::atomic<bool> stop{false};

    // Reader: snapshot + check all 4 trigger fields agree.
    std::thread reader([&]() {
        for (int i = 0; i < kReaderIterations; ++i) {
            auto snap = field.load(std::memory_order_acquire);
            ASSERT_NE(snap, nullptr);
            const bool s = snap->macroTriggerSpace;
            const bool e = snap->macroTriggerEnter;
            const bool t = snap->macroTriggerTab;
            const bool d = snap->macroTriggerDir;
            // All-true (cfgA) or all-false (cfgB) — never a mix.
            EXPECT_TRUE((s && e && t && d) || (!s && !e && !t && !d))
                << "Torn read at iteration " << i << ": "
                << "space=" << s << " enter=" << e << " tab=" << t << " dir=" << d;
        }
        stop.store(true, std::memory_order_release);
    });

    // Writer: alternate between cfgA and cfgB until reader finishes.
    int writes = 0;
    while (!stop.load(std::memory_order_acquire)) {
        field.store((writes & 1) ? sharedB : sharedA, std::memory_order_release);
        ++writes;
    }

    reader.join();
    // Sanity check — writer should have published many times.
    EXPECT_GT(writes, 100);
}

}  // namespace
}  // namespace NextKey
