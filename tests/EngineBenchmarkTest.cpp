// VKey - Engine CPU Benchmark Tests
// SPDX-License-Identifier: AGPL-3.0-only
// Measures per-keystroke latency and throughput for Telex and VNI input methods.

#include <gtest/gtest.h>
#include "core/engine/TypingEngine.h"
#include "core/config/TypingConfig.h"
#include "TestHelper.h"
#include <chrono>
#include <numeric>
#include <algorithm>
#include <vector>
#include <string>

namespace NextKey {
namespace {

using Clock = std::chrono::high_resolution_clock;
using Nanoseconds = std::chrono::nanoseconds;
using Testing::TypeString;

// ---------------------------------------------------------------------------
// Benchmark infrastructure
// ---------------------------------------------------------------------------

struct BenchmarkResult {
    double avgNs;
    double medianNs;
    double p95Ns;
    double p99Ns;
    double minNs;
    double maxNs;
    size_t totalOps;
};

template<typename Fn>
[[nodiscard]] BenchmarkResult RunBenchmark(Fn&& fn, size_t iterations) {
    std::vector<double> durations;
    durations.reserve(iterations);

    // Warmup
    const size_t warmup = std::max<size_t>(10, iterations / 10);
    for (size_t i = 0; i < warmup; ++i) {
        fn();
    }

    for (size_t i = 0; i < iterations; ++i) {
        auto start = Clock::now();
        fn();
        auto end = Clock::now();
        durations.push_back(static_cast<double>(
            std::chrono::duration_cast<Nanoseconds>(end - start).count()));
    }

    std::sort(durations.begin(), durations.end());
    double sum = std::accumulate(durations.begin(), durations.end(), 0.0);

    BenchmarkResult r{};
    r.totalOps = iterations;
    r.avgNs = sum / static_cast<double>(iterations);
    r.medianNs = durations[iterations / 2];
    r.p95Ns = durations[static_cast<size_t>(iterations * 0.95)];
    r.p99Ns = durations[static_cast<size_t>(iterations * 0.99)];
    r.minNs = durations.front();
    r.maxNs = durations.back();
    return r;
}

void PrintResult(const char* name, const BenchmarkResult& r) {
    printf("  %-40s avg=%7.0f ns  median=%7.0f ns  p95=%7.0f ns  p99=%7.0f ns  [%zu ops]\n",
           name, r.avgNs, r.medianNs, r.p95Ns, r.p99Ns, r.totalOps);
}

// ---------------------------------------------------------------------------
// Test data: realistic Vietnamese Telex keystrokes
// ---------------------------------------------------------------------------

const wchar_t* kTelexWords[] = {
    L"vieejt",    // việt
    L"nam",       // nam
    L"xin",       // xin
    L"chaof",     // chào
    L"moij",      // mọi
    L"nguwowif",  // người
    L"ddaay",     // đây
    L"laaf",      // là
    L"mootj",     // một
    L"thuwf",     // thử
    L"nghieem",   // nghiêm
    L"tuwj",      // tự
    L"ddoongj",   // động
    L"baawn",     // bẩn
    L"truwowngf", // trường
    L"hoocj",     // học
};

const wchar_t* kTelexSentence =
    L"xin chaof, tooi laaf mootj laapj trinhf vieen vieejt nam";

const wchar_t* kEnglishSentence =
    L"the quick brown fox jumps over the lazy dog";

// ---------------------------------------------------------------------------
// Telex Engine Benchmarks
// ---------------------------------------------------------------------------

class EngineBenchmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.inputMethod = InputMethod::Telex;
        config_.spellCheckEnabled = false;
        config_.optimizeLevel = 0;
    }

    TypingConfig config_;
};

TEST_F(EngineBenchmarkTest, Telex_SinglePushChar) {
    TypingEngine engine(config_);
    auto r = RunBenchmark([&]() {
        engine.Reset();
        engine.PushChar(L'a');
    }, 100000);
    PrintResult("Telex single PushChar", r);
    EXPECT_LT(r.p95Ns, 5000.0) << "Single PushChar should be < 5 us at p95";
}

TEST_F(EngineBenchmarkTest, Telex_ShortWord_TypeAndPeek) {
    TypingEngine engine(config_);
    auto r = RunBenchmark([&]() {
        engine.Reset();
        TypeString(engine, L"vieejt");
        auto result = engine.Peek();
        (void)result;
    }, 50000);
    PrintResult("Telex 'viet' type+peek", r);
    EXPECT_LT(r.p95Ns, 10000.0) << "Short word should be < 10 us at p95";
}

TEST_F(EngineBenchmarkTest, Telex_ShortWord_TypeAndCommit) {
    TypingEngine engine(config_);
    auto r = RunBenchmark([&]() {
        engine.Reset();
        TypeString(engine, L"truwowngf");
        auto result = engine.Commit();
        (void)result;
    }, 50000);
    PrintResult("Telex 'truong' type+commit", r);
    EXPECT_LT(r.p95Ns, 15000.0) << "Complex word should be < 15 us at p95";
}

TEST_F(EngineBenchmarkTest, Telex_CommonWords_Throughput) {
    TypingEngine engine(config_);
    const size_t wordCount = sizeof(kTelexWords) / sizeof(kTelexWords[0]);

    auto r = RunBenchmark([&]() {
        for (size_t i = 0; i < wordCount; ++i) {
            engine.Reset();
            TypeString(engine, kTelexWords[i]);
            auto result = engine.Commit();
            (void)result;
        }
    }, 10000);

    double perWordNs = r.avgNs / static_cast<double>(wordCount);
    printf("  %-40s per-word avg=%7.0f ns (%zu words/iter)\n",
           "Telex common words throughput", perWordNs, wordCount);
    EXPECT_LT(perWordNs, 15000.0) << "Per-word average should be < 15 us";
}

TEST_F(EngineBenchmarkTest, Telex_Sentence_Throughput) {
    TypingEngine engine(config_);
    const std::wstring sentence(kTelexSentence);
    const size_t charCount = sentence.size();

    auto r = RunBenchmark([&]() {
        engine.Reset();
        for (wchar_t c : sentence) {
            if (c == L' ' || c == L',') {
                (void)engine.Commit();
                engine.Reset();
            } else {
                engine.PushChar(c);
            }
        }
        (void)engine.Commit();
    }, 20000);

    double perCharNs = r.avgNs / static_cast<double>(charCount);
    double keysPerSec = 1e9 / perCharNs;
    printf("  %-40s per-char=%5.0f ns  ~%.0f keys/sec  total=%7.0f ns\n",
           "Telex sentence throughput", perCharNs, keysPerSec, r.avgNs);
    EXPECT_LT(perCharNs, 2000.0) << "Per-char should be < 2 us";
}

TEST_F(EngineBenchmarkTest, Telex_WithSpellCheck) {
    config_.spellCheckEnabled = true;
    TypingEngine engine(config_);

    auto r = RunBenchmark([&]() {
        engine.Reset();
        TypeString(engine, L"truwowngf");
        auto result = engine.Commit();
        (void)result;
    }, 50000);
    PrintResult("Telex 'truong' +spellcheck", r);
    EXPECT_LT(r.p95Ns, 20000.0) << "With spell check should be < 20 us at p95";
}

TEST_F(EngineBenchmarkTest, Telex_EnglishPassthrough) {
    config_.spellCheckEnabled = true;
    TypingEngine engine(config_);
    const std::wstring text(kEnglishSentence);
    const size_t charCount = text.size();

    auto r = RunBenchmark([&]() {
        engine.Reset();
        for (wchar_t c : text) {
            if (c == L' ') {
                (void)engine.Commit();
                engine.Reset();
            } else {
                engine.PushChar(c);
            }
        }
        (void)engine.Commit();
    }, 20000);

    double perCharNs = r.avgNs / static_cast<double>(charCount);
    printf("  %-40s per-char=%5.0f ns  total=%7.0f ns\n",
           "Telex English passthrough", perCharNs, r.avgNs);
    EXPECT_LT(perCharNs, 2000.0) << "English passthrough should be < 2 us per char";
}

TEST_F(EngineBenchmarkTest, Telex_BackspaceUndo) {
    TypingEngine engine(config_);
    auto r = RunBenchmark([&]() {
        engine.Reset();
        TypeString(engine, L"truwowngf");
        for (int i = 0; i < 6; ++i) {
            engine.Backspace();
        }
    }, 50000);
    PrintResult("Telex type+backspace undo", r);
    EXPECT_LT(r.p95Ns, 15000.0) << "Type+undo should be < 15 us at p95";
}

TEST_F(EngineBenchmarkTest, Telex_PeekCost) {
    TypingEngine engine(config_);
    TypeString(engine, L"truwowngf");

    auto r = RunBenchmark([&]() {
        auto result = engine.Peek();
        (void)result;
    }, 200000);
    PrintResult("Telex Peek() only", r);
    EXPECT_LT(r.p95Ns, 2000.0) << "Peek should be < 2 us at p95";
}

// ---------------------------------------------------------------------------
// VNI Engine Benchmark (for comparison)
// ---------------------------------------------------------------------------

TEST_F(EngineBenchmarkTest, Vni_ShortWord_TypeAndCommit) {
    config_.inputMethod = InputMethod::VNI;
    TypingEngine engine(config_);

    auto r = RunBenchmark([&]() {
        engine.Reset();
        TypeString(engine, L"tru7o7ng2");
        auto result = engine.Commit();
        (void)result;
    }, 50000);
    PrintResult("VNI 'truong' type+commit", r);
}

// ---------------------------------------------------------------------------
// Sustained typing simulation (most realistic benchmark)
// ---------------------------------------------------------------------------

TEST_F(EngineBenchmarkTest, Telex_SustainedTyping_WithSpellCheck) {
    config_.spellCheckEnabled = true;
    TypingEngine engine(config_);

    const size_t wordCount = sizeof(kTelexWords) / sizeof(kTelexWords[0]);
    size_t totalKeystrokes = 0;
    for (size_t i = 0; i < wordCount; ++i) {
        totalKeystrokes += wcslen(kTelexWords[i]);
    }

    auto r = RunBenchmark([&]() {
        for (size_t i = 0; i < wordCount; ++i) {
            engine.Reset();
            TypeString(engine, kTelexWords[i]);
            auto result = engine.Commit();
            (void)result;
        }
    }, 5000);

    double perKeystroke = r.avgNs / static_cast<double>(totalKeystrokes);
    double keysPerSec = 1e9 / perKeystroke;

    printf("\n  === SUSTAINED TYPING SUMMARY (spell check ON) ===\n");
    printf("  Words: %zu   Keystrokes: %zu\n", wordCount, totalKeystrokes);
    printf("  Per-keystroke: %.0f ns (%.0f keys/sec)\n", perKeystroke, keysPerSec);
    printf("  Per-word avg:  %.0f ns\n", r.avgNs / static_cast<double>(wordCount));
    printf("  Total (all words): avg=%.0f ns  p95=%.0f ns  p99=%.0f ns\n",
           r.avgNs, r.p95Ns, r.p99Ns);

    // 100 WPM = ~8.3 chars/sec = 120ms between keys
    // Engine must be orders of magnitude faster
    EXPECT_LT(perKeystroke, 5000.0)
        << "Per-keystroke must be < 5 us for smooth typing";
}

TEST_F(EngineBenchmarkTest, Telex_SpellExclusions_Overhead) {
    config_.spellCheckEnabled = true;
    config_.spellExclusions = {L"hđ", L"hđqt", L"đt", L"fôn", L"jắc", L"btv", L"thpt", L"đh"};
    TypingEngine engine(config_);

    const size_t wordCount = sizeof(kTelexWords) / sizeof(kTelexWords[0]);
    size_t totalKeystrokes = 0;
    for (size_t i = 0; i < wordCount; ++i)
        totalKeystrokes += wcslen(kTelexWords[i]);

    auto r = RunBenchmark([&]() {
        for (size_t i = 0; i < wordCount; ++i) {
            engine.Reset();
            TypeString(engine, kTelexWords[i]);
            auto result = engine.Commit();
            (void)result;
        }
    }, 5000);

    double perKeystroke = r.avgNs / static_cast<double>(totalKeystrokes);
    printf("  %-40s per-key=%5.0f ns  (excl=%zu patterns)\n",
           "Telex spell exclusions overhead", perKeystroke, config_.spellExclusions.size());
    EXPECT_LT(perKeystroke, 5000.0);
}

}  // namespace
}  // namespace NextKey
