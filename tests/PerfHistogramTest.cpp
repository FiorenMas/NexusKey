// PerfHistogramTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Phase 1 (per-stage histogram) verification — see
// docs/plans/2026-05-19-architecture-review-design.md §Phase 1.
//
// Locked-in contract for the 8-bucket log-scale histogram + Enabled() gate
// + counter-thread-safety + flush format + privacy (counter-only output).
// Linux-portable: no Win32 calls inside Perf::Histogram public API.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app/system/PerfHistogram.h"

namespace NextKey::Perf {
namespace {

namespace fs = std::filesystem;

class PerfHistogramTest : public ::testing::Test {
protected:
    void SetUp() override {
        Histogram::Reset();
        Histogram::SetEnabled(false);
        logPath_ = fs::temp_directory_path() /
                   (std::string("perf-hist-test-") +
                    std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                    "-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".log");
        // Convert fs::path -> std::wstring for the API.
        Histogram::SetLogPath(logPath_.wstring());
        std::error_code ec;
        fs::remove(logPath_, ec);
    }
    void TearDown() override {
        Histogram::Stop();
        Histogram::Reset();
        Histogram::SetEnabled(false);
        std::error_code ec;
        fs::remove(logPath_, ec);
    }

    fs::path logPath_;
};

// ──────────────────────────────────────────────────────────────────────────
// Bucket boundary correctness (Phase 1 design table)
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, BucketBoundariesLogScale) {
    // Design boundaries (ns): <1µs, <16µs, <256µs, <1ms, <4ms, <16ms, <64ms, ≥64ms.
    EXPECT_EQ(Histogram::BucketIndex(0),               0u);  // 0ns → bucket 0
    EXPECT_EQ(Histogram::BucketIndex(500),             0u);  // 500ns → <1µs
    EXPECT_EQ(Histogram::BucketIndex(999),             0u);
    EXPECT_EQ(Histogram::BucketIndex(1'000),           1u);  // 1µs boundary
    EXPECT_EQ(Histogram::BucketIndex(15'999),          1u);
    EXPECT_EQ(Histogram::BucketIndex(16'000),          2u);  // 16µs boundary
    EXPECT_EQ(Histogram::BucketIndex(255'999),         2u);
    EXPECT_EQ(Histogram::BucketIndex(256'000),         3u);  // 256µs boundary
    EXPECT_EQ(Histogram::BucketIndex(999'999),         3u);
    EXPECT_EQ(Histogram::BucketIndex(1'000'000),       4u);  // 1ms boundary
    EXPECT_EQ(Histogram::BucketIndex(3'999'999),       4u);
    EXPECT_EQ(Histogram::BucketIndex(4'000'000),       5u);  // 4ms boundary
    EXPECT_EQ(Histogram::BucketIndex(15'999'999),      5u);
    EXPECT_EQ(Histogram::BucketIndex(16'000'000),      6u);  // 16ms boundary
    EXPECT_EQ(Histogram::BucketIndex(63'999'999),      6u);
    EXPECT_EQ(Histogram::BucketIndex(64'000'000),      7u);  // 64ms boundary
    EXPECT_EQ(Histogram::BucketIndex(1'000'000'000ULL), 7u); // 1s → top open bucket
}

TEST_F(PerfHistogramTest, BucketUpperBoundsMatchDesign) {
    // First 7 buckets have finite upper bounds; the 8th (idx 7) is open.
    EXPECT_EQ(Histogram::BucketUpperBoundNs(0), 1'000ULL);
    EXPECT_EQ(Histogram::BucketUpperBoundNs(1), 16'000ULL);
    EXPECT_EQ(Histogram::BucketUpperBoundNs(2), 256'000ULL);
    EXPECT_EQ(Histogram::BucketUpperBoundNs(3), 1'000'000ULL);
    EXPECT_EQ(Histogram::BucketUpperBoundNs(4), 4'000'000ULL);
    EXPECT_EQ(Histogram::BucketUpperBoundNs(5), 16'000'000ULL);
    EXPECT_EQ(Histogram::BucketUpperBoundNs(6), 64'000'000ULL);
    EXPECT_EQ(Histogram::BucketUpperBoundNs(7), 0ULL);  // open
}

// ──────────────────────────────────────────────────────────────────────────
// Seven stages locked per Phase 1 design table
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, StageCountIsSeven) {
    EXPECT_EQ(kStageCount, 7u);
    EXPECT_EQ(static_cast<size_t>(Stage::Count), 7u);
}

TEST_F(PerfHistogramTest, StageNamesAreStable) {
    EXPECT_STREQ(Histogram::StageName(Stage::EnginePush),    "engine_push");
    EXPECT_STREQ(Histogram::StageName(Stage::TopGuard),      "top_guard");
    EXPECT_STREQ(Histogram::StageName(Stage::Injector),      "injector");
    EXPECT_STREQ(Histogram::StageName(Stage::Replace),       "replace");
    EXPECT_STREQ(Histogram::StageName(Stage::FocusClassify), "focus_classify");
    EXPECT_STREQ(Histogram::StageName(Stage::ConfigReload),  "config_reload");
    EXPECT_STREQ(Histogram::StageName(Stage::TotalKeydown),  "total_keydown");
}

// ──────────────────────────────────────────────────────────────────────────
// Record counts ALWAYS (gate is in Scope, not Record). Single-thread sanity.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, RecordIncrementsTargetBucket) {
    Histogram::Record(Stage::EnginePush, 500);              // bucket 0
    Histogram::Record(Stage::EnginePush, 500);
    Histogram::Record(Stage::EnginePush, 10'000);           // bucket 1
    Histogram::Record(Stage::Replace,    20'000'000);       // bucket 6

    EXPECT_EQ(Histogram::CountFor(Stage::EnginePush, 0), 2u);
    EXPECT_EQ(Histogram::CountFor(Stage::EnginePush, 1), 1u);
    EXPECT_EQ(Histogram::CountFor(Stage::EnginePush, 2), 0u);
    EXPECT_EQ(Histogram::CountFor(Stage::Replace,    6), 1u);
}

// ──────────────────────────────────────────────────────────────────────────
// Enabled() gate gates the Scope (PERF_SCOPE) — Record itself is unaffected.
// Verifying the Scope helper here keeps the gate contract honest.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, ScopeRespectsEnabledGate) {
    // Disabled by default — Scope must NOT record anything.
    EXPECT_FALSE(Histogram::Enabled());
    {
        Scope s(Stage::EnginePush);
        // Sleep long enough that any non-zero bucket would land in 1µs+.
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    uint64_t totalBefore = 0;
    for (size_t b = 0; b < kBucketCount; ++b) {
        totalBefore += Histogram::CountFor(Stage::EnginePush, b);
    }
    EXPECT_EQ(totalBefore, 0u);

    Histogram::SetEnabled(true);
    EXPECT_TRUE(Histogram::Enabled());
    {
        Scope s(Stage::EnginePush);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    uint64_t totalAfter = 0;
    for (size_t b = 0; b < kBucketCount; ++b) {
        totalAfter += Histogram::CountFor(Stage::EnginePush, b);
    }
    EXPECT_EQ(totalAfter, 1u);  // exactly one record landed somewhere
}

// ──────────────────────────────────────────────────────────────────────────
// Thread-safe atomic counter increment. Two threads, kPerThread Records each,
// landing in the same bucket. Result must equal 2 * kPerThread (no lost updates).
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, RecordThreadSafetyNoLostUpdates) {
    constexpr int kThreads    = 4;
    constexpr int kPerThread  = 25'000;
    constexpr uint64_t kDelta = 500;  // bucket 0
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&]() {
            ready.fetch_add(1);
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int i = 0; i < kPerThread; ++i) {
                Histogram::Record(Stage::TotalKeydown, kDelta);
            }
        });
    }
    while (ready.load() != kThreads) std::this_thread::yield();
    go.store(true, std::memory_order_release);
    for (auto& t : ts) t.join();

    EXPECT_EQ(Histogram::CountFor(Stage::TotalKeydown, 0),
              static_cast<uint64_t>(kThreads) * kPerThread);
}

// ──────────────────────────────────────────────────────────────────────────
// Flush format: TSV `stage<TAB>bucket_us<TAB>count<TAB>epoch_secs`.
// Privacy: only numeric/identifier tokens — no key/text/HWND/title data.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, FlushWritesTsvFormat) {
    Histogram::Record(Stage::EnginePush, 500);          // bucket 0
    Histogram::Record(Stage::Replace,    20'000'000);   // bucket 6
    ASSERT_TRUE(Histogram::Flush());

    std::ifstream in(logPath_);
    ASSERT_TRUE(in.is_open()) << "log file not created: " << logPath_;

    bool foundEnginePush0 = false;
    bool foundReplace6    = false;
    std::string line;
    // TSV regex: stage_name<TAB>bucket_us_or_inf<TAB>count<TAB>epoch_secs
    // bucket_us is either an unsigned integer (the upper bound in µs) or "inf"
    // for the open bucket; epoch_secs is a unsigned integer (Unix seconds).
    std::regex tsv(R"(^([a-z_]+)\t(\d+|inf)\t(\d+)\t(\d+)$)");
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::smatch m;
        ASSERT_TRUE(std::regex_match(line, m, tsv))
            << "line does not match TSV contract: " << line;
        const std::string stage  = m[1];
        const std::string bucket = m[2];
        const uint64_t count     = std::stoull(m[3]);
        if (stage == "engine_push" && bucket == "1" && count == 1) {
            foundEnginePush0 = true;
        }
        if (stage == "replace" && bucket == "64000" && count == 1) {
            foundReplace6 = true;
        }
    }
    EXPECT_TRUE(foundEnginePush0) << "engine_push bucket<1µs row missing";
    EXPECT_TRUE(foundReplace6)    << "replace bucket<64ms row missing";
}

TEST_F(PerfHistogramTest, FlushIsCounterOnlyNoIdentifiers) {
    // Privacy gate: log must never contain HWND-style hex, key char codes,
    // window titles, or text. Only [a-z_], digits, "inf", and tab/newline.
    Histogram::Record(Stage::EnginePush, 500);
    Histogram::Record(Stage::FocusClassify, 200'000);
    ASSERT_TRUE(Histogram::Flush());

    std::ifstream in(logPath_);
    ASSERT_TRUE(in.is_open());
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string content = buf.str();

    // No hex pointers (0x...).
    EXPECT_EQ(content.find("0x"), std::string::npos)
        << "log must not contain hex tokens";
    // No path separators / drive letters — would indicate window-class leakage.
    EXPECT_EQ(content.find('\\'), std::string::npos);
    EXPECT_EQ(content.find(':'),  std::string::npos);
    // Every non-empty, non-comment line MUST match the TSV grammar.
    std::regex tsv(R"(^([a-z_]+)\t(\d+|inf)\t(\d+)\t(\d+)$)");
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || line[0] == '#') continue;
        EXPECT_TRUE(std::regex_match(line, tsv))
            << "non-counter content leaked into log: " << line;
    }
}

TEST_F(PerfHistogramTest, FlushDumpsAllStagesAndAllBuckets) {
    Histogram::Record(Stage::TotalKeydown, 500);
    ASSERT_TRUE(Histogram::Flush());

    std::ifstream in(logPath_);
    ASSERT_TRUE(in.is_open());
    std::string line;
    std::vector<std::string> stages;
    int rowCount = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        ++rowCount;
        // Capture stage column
        auto tab = line.find('\t');
        if (tab != std::string::npos) stages.push_back(line.substr(0, tab));
    }
    // 7 stages × 8 buckets = 56 rows minimum (one per cell, including zero-count cells)
    EXPECT_EQ(rowCount, static_cast<int>(kStageCount) * static_cast<int>(kBucketCount));
}

// ──────────────────────────────────────────────────────────────────────────
// MaybeFlush() respects the 60s throttle and the Enabled() gate.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, MaybeFlushDoesNothingWhenDisabled) {
    Histogram::SetEnabled(false);
    Histogram::Record(Stage::EnginePush, 500);
    Histogram::MaybeFlush();
    EXPECT_FALSE(fs::exists(logPath_))
        << "MaybeFlush() must not write while disabled";
}

TEST_F(PerfHistogramTest, MaybeFlushHonoursThrottle) {
    Histogram::SetEnabled(true);
    Histogram::Record(Stage::EnginePush, 500);
    // First MaybeFlush either flushes or no-ops depending on whether the
    // "first flush" semantics is "flush immediately" or "wait one period";
    // the contract only requires that we don't double-write within 60s.
    Histogram::MaybeFlush();
    std::error_code ec;
    fs::remove(logPath_, ec);
    Histogram::Record(Stage::EnginePush, 500);
    Histogram::MaybeFlush();  // second call within throttle window
    EXPECT_FALSE(fs::exists(logPath_))
        << "MaybeFlush() must throttle to once per 60s window";
}

// ──────────────────────────────────────────────────────────────────────────
// Stop() flushes a final snapshot and is idempotent.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, StopFlushesFinalSnapshot) {
    Histogram::SetEnabled(true);
    Histogram::Record(Stage::TopGuard, 8'000);
    Histogram::Stop();
    EXPECT_TRUE(fs::exists(logPath_));
    // Second Stop() must not error.
    Histogram::Stop();
}

// ──────────────────────────────────────────────────────────────────────────
// Reset() zeroes all counters.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(PerfHistogramTest, ResetClearsAllCounters) {
    Histogram::Record(Stage::Injector, 2'000'000);
    EXPECT_NE(Histogram::CountFor(Stage::Injector, 4), 0u);
    Histogram::Reset();
    for (size_t s = 0; s < kStageCount; ++s) {
        for (size_t b = 0; b < kBucketCount; ++b) {
            EXPECT_EQ(Histogram::CountFor(static_cast<Stage>(s), b), 0u);
        }
    }
}

}  // namespace
}  // namespace NextKey::Perf
