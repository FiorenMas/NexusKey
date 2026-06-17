// VKey - PerfHistogram impl
// SPDX-License-Identifier: AGPL-3.0-only
//
// See PerfHistogram.h for the design contract.

#include "app/system/PerfHistogram.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <ios>
#include <mutex>
#include <string>

namespace NextKey::Perf {

namespace {

// 8-bucket log-scale boundaries in nanoseconds. Indexed [0..6]; the final
// bucket (idx 7) is open-ended ("≥64ms"). Boundaries are inclusive of the
// previous bucket and exclusive of the named cliff — BucketIndex(1'000) = 1
// (the first ns ≥1µs lands in bucket 1, not bucket 0).
constexpr std::array<std::uint64_t, kBucketCount - 1> kBucketUpperBoundsNs = {
    1'000ULL,         // <1µs   — Tier 1 engine cliff
    16'000ULL,        // <16µs
    256'000ULL,       // <256µs
    1'000'000ULL,     // <1ms   — stage budget cliff
    4'000'000ULL,     // <4ms
    16'000'000ULL,    // <16ms  — chaos p99 cliff (current worst-case 14–17ms)
    64'000'000ULL,    // <64ms  — LowLevelHooksTimeout/3 ≈ 100ms danger cliff
};

constexpr std::array<const char*, kStageCount> kStageNames = {
    "engine_push",
    "top_guard",
    "injector",
    "replace",
    "focus_classify",
    "config_reload",
    "total_keydown",
};

// Counters. `std::atomic<uint64_t>` is lock-free on every platform we ship to;
// MSVC and libstdc++ both back it with a native 64-bit instruction on x64.
std::atomic<std::uint64_t> g_counts[kStageCount][kBucketCount]{};
std::atomic<bool>          g_enabled{false};

// Flush state. The mutex serialises file I/O between callers (60s tick + an
// out-of-band Stop()) so two writers cannot interleave bytes. It is NEVER
// taken on the hot path — only Record/Enabled/BucketIndex sit there, all
// lock-free.
std::mutex                          g_flushMutex;
std::wstring                        g_logPath;
std::chrono::steady_clock::time_point g_lastFlush{};  // epoch == "never flushed"

constexpr auto kFlushInterval = std::chrono::seconds(60);

// Take a non-atomic snapshot of all counters. Uses relaxed loads — flush
// only needs a single-pass aggregate and tolerates the rare in-flight
// update being deferred to the next snapshot. See Phase 1 design §
// "Record path" (counter-only, no consistency requirement across cells).
void SnapshotCounts(std::uint64_t out[kStageCount][kBucketCount]) noexcept {
    for (std::size_t s = 0; s < kStageCount; ++s) {
        for (std::size_t b = 0; b < kBucketCount; ++b) {
            out[s][b] = g_counts[s][b].load(std::memory_order_relaxed);
        }
    }
}

// Encode the bucket's upper bound for the TSV `bucket_us` column. The open
// bucket emits the sentinel "inf" so awk/sort scripts can distinguish it
// from real values without losing the column type.
void WriteBucketLabel(std::ostream& os, std::size_t bucket) noexcept {
    if (bucket >= kBucketCount - 1) {
        os << "inf";
        return;
    }
    const std::uint64_t boundNs = kBucketUpperBoundsNs[bucket];
    os << (boundNs / 1000ULL);  // µs
}

}  // namespace

// ────────────────────────────────────────────────────────────────────────────
// Hot path
// ────────────────────────────────────────────────────────────────────────────
void Histogram::Record(Stage stage, std::uint64_t deltaNs) noexcept {
    const std::size_t s = static_cast<std::size_t>(stage);
    if (s >= kStageCount) return;  // defensive — stage enum stays trusted
    const std::size_t b = BucketIndex(deltaNs);
    g_counts[s][b].fetch_add(1, std::memory_order_relaxed);
}

bool Histogram::Enabled() noexcept {
    return g_enabled.load(std::memory_order_acquire);
}

void Histogram::SetEnabled(bool enabled) noexcept {
    g_enabled.store(enabled, std::memory_order_release);
}

// ────────────────────────────────────────────────────────────────────────────
// Bucket math
// ────────────────────────────────────────────────────────────────────────────
std::size_t Histogram::BucketIndex(std::uint64_t deltaNs) noexcept {
    for (std::size_t b = 0; b < kBucketUpperBoundsNs.size(); ++b) {
        if (deltaNs < kBucketUpperBoundsNs[b]) return b;
    }
    return kBucketCount - 1;  // open bucket
}

std::uint64_t Histogram::BucketUpperBoundNs(std::size_t bucket) noexcept {
    if (bucket >= kBucketUpperBoundsNs.size()) return 0ULL;  // open
    return kBucketUpperBoundsNs[bucket];
}

const char* Histogram::StageName(Stage stage) noexcept {
    const std::size_t s = static_cast<std::size_t>(stage);
    if (s >= kStageCount) return "?";
    return kStageNames[s];
}

// ────────────────────────────────────────────────────────────────────────────
// Test / introspection helpers
// ────────────────────────────────────────────────────────────────────────────
std::uint64_t Histogram::CountFor(Stage stage, std::size_t bucket) noexcept {
    const std::size_t s = static_cast<std::size_t>(stage);
    if (s >= kStageCount || bucket >= kBucketCount) return 0ULL;
    return g_counts[s][bucket].load(std::memory_order_relaxed);
}

void Histogram::Reset() noexcept {
    for (std::size_t s = 0; s < kStageCount; ++s) {
        for (std::size_t b = 0; b < kBucketCount; ++b) {
            g_counts[s][b].store(0, std::memory_order_relaxed);
        }
    }
    std::lock_guard<std::mutex> lock(g_flushMutex);
    g_lastFlush = std::chrono::steady_clock::time_point{};
}

// ────────────────────────────────────────────────────────────────────────────
// Flush
// ────────────────────────────────────────────────────────────────────────────
void Histogram::SetLogPath(const std::wstring& path) noexcept {
    std::lock_guard<std::mutex> lock(g_flushMutex);
    g_logPath = path;
}

bool Histogram::Flush() noexcept {
    std::lock_guard<std::mutex> lock(g_flushMutex);
    if (g_logPath.empty()) return false;

    std::uint64_t snap[kStageCount][kBucketCount];
    SnapshotCounts(snap);

    // Append. On Linux test runs the path lands in /tmp; on Windows the
    // caller wires `%APPDATA%/VKey/perf-histogram-<pid>-<startTs>.log`.
    // std::wofstream is portable; std::ofstream takes a wide path only on
    // MSVC, so go through a wstring → narrow conversion for libstdc++.
#ifdef _WIN32
    std::ofstream out(g_logPath, std::ios::app | std::ios::binary);
#else
    std::string narrowPath(g_logPath.begin(), g_logPath.end());
    std::ofstream out(narrowPath, std::ios::app | std::ios::binary);
#endif
    if (!out.is_open()) return false;

    const auto epochSecs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Emit one row per (stage, bucket). Bucket label is µs upper bound or
    // "inf" for the open bucket. Counter-only — privacy by construction.
    for (std::size_t s = 0; s < kStageCount; ++s) {
        for (std::size_t b = 0; b < kBucketCount; ++b) {
            out << kStageNames[s] << '\t';
            WriteBucketLabel(out, b);
            out << '\t' << snap[s][b] << '\t' << epochSecs << '\n';
        }
    }
    out.flush();
    g_lastFlush = std::chrono::steady_clock::now();
    return static_cast<bool>(out);
}

void Histogram::MaybeFlush() noexcept {
    if (!Enabled()) return;
    {
        std::lock_guard<std::mutex> lock(g_flushMutex);
        const auto now = std::chrono::steady_clock::now();
        if (g_lastFlush.time_since_epoch().count() != 0 &&
            (now - g_lastFlush) < kFlushInterval) {
            return;
        }
    }
    (void)Flush();
}

void Histogram::Stop() noexcept {
    // Final dump (idempotent — second call sees zero counters after Reset).
    (void)Flush();
    Reset();
    SetEnabled(false);
}

}  // namespace NextKey::Perf
