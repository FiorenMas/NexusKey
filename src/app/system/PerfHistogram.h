// VKey - Per-stage performance histogram
// SPDX-License-Identifier: AGPL-3.0-only
//
// Phase 1 of the 2026-05-19 architecture review design
// (docs/plans/2026-05-19-architecture-review-design.md). Captures p50/p95/p99/
// max per hook-pipeline stage as a gold reference BEFORE any ownership change.
//
// Design highlights (see plan §Phase 1):
//   - 8-bucket log-scale histogram per stage (Tier 1 cliff at 1µs, Tier 2 cliff
//     at 30ms, hard ceiling at 64ms).
//   - Stack-only RAII Scope helper measuring wall-clock via std::chrono
//     steady_clock — Rule 11.2 forbids heap on the hook hot path.
//   - Enabled() gate read inside Scope's ctor so disabled runs cost ~one atomic
//     load + a branch; QueryPerformanceCounter overhead is never paid.
//   - Counter-only flush format: privacy-by-construction, no key/text/HWND
//     content can leak.
//
// Linux-portable: no Win32 calls in the public API. The Scope helper uses
// std::chrono::steady_clock, which delegates to QPC on Windows and to
// clock_gettime(CLOCK_MONOTONIC) on Linux.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace NextKey::Perf {

/// Pipeline stages instrumented by Phase 1. Order is stable — flush output
/// references stages by name, but BucketIndex(_)/CountFor() use the enum
/// index, so do NOT reorder existing entries.
enum class Stage : uint8_t {
    EnginePush = 0,     // engine_->PushChar + Peek (Tier 1 budget: <1µs)
    TopGuard,           // RunTopGuards entry → return (<16µs)
    Injector,           // injector_->Replace inner call (<4ms)
    Replace,            // ReplaceComposition entry → return (<16ms)
    FocusClassify,      // ClassifyFocusedWindow (<5ms)
    ConfigReload,       // ReloadFromToml body (<50ms; Phase 3 will move off hook)
    TotalKeydown,       // LL callback entry → return (<30ms p99, Tier 2 budget)
    Count
};

constexpr std::size_t kStageCount  = static_cast<std::size_t>(Stage::Count);
constexpr std::size_t kBucketCount = 8;

/// Free-function API. Histogram state is process-singleton (file-scope atomic
/// counters) — there is exactly one set of counters per process, no instance
/// to construct. This keeps the hot path branchless past the Enabled() check.
class Histogram {
public:
    // ── Hot-path entries (called from PERF_SCOPE) ──────────────────────────
    /// Increment the bucket the given delta lands in for `stage`. Lock-free
    /// (single atomic fetch_add). Safe under any concurrency level.
    static void Record(Stage stage, std::uint64_t deltaNs) noexcept;

    /// True iff the histogram is currently capturing samples. Read by Scope
    /// ctor; setters are main-thread / config-reader.
    [[nodiscard]] static bool Enabled() noexcept;
    static void SetEnabled(bool enabled) noexcept;

    // ── Bucket math (pure functions) ───────────────────────────────────────
    /// Map a delta in nanoseconds to one of [0, kBucketCount) using the
    /// log-scale boundaries baked into the design table.
    [[nodiscard]] static std::size_t BucketIndex(std::uint64_t deltaNs) noexcept;

    /// Upper bound of bucket `b` in nanoseconds. Returns 0 for the open
    /// (final) bucket. Used by flush to emit µs labels.
    [[nodiscard]] static std::uint64_t BucketUpperBoundNs(std::size_t bucket) noexcept;

    /// Human-readable stage label (stable; appears in flush TSV).
    [[nodiscard]] static const char* StageName(Stage stage) noexcept;

    // ── Test / introspection helpers ──────────────────────────────────────
    /// Counter for (stage, bucket). Non-atomic read of an atomic counter —
    /// snapshot semantics are sufficient for histogram aggregation.
    [[nodiscard]] static std::uint64_t CountFor(Stage stage, std::size_t bucket) noexcept;

    /// Zero every counter. Test-only — production code never resets in flight.
    static void Reset() noexcept;

    // ── Flush ──────────────────────────────────────────────────────────────
    /// Configure the output file path. Empty path disables flushing.
    /// Caller-provided (HookEngine::Start composes
    /// `%APPDATA%/VKey/perf-histogram-<pid>-<startTs>.log`).
    static void SetLogPath(const std::wstring& path) noexcept;

    /// Append a full snapshot (all stages × all buckets) to the configured
    /// log path. TSV: `stage<TAB>bucket_us<TAB>count<TAB>epoch_secs`. Returns
    /// false if the path is empty or the file cannot be opened.
    static bool Flush() noexcept;

    /// Flush() iff Enabled() AND ≥60s since the last successful flush.
    /// Caller-driven cadence — e.g. HookEngine::OnTickPoll polls this.
    static void MaybeFlush() noexcept;

    /// Final flush + counter reset. Idempotent (safe to call multiple times).
    /// Wired into HookEngine::Stop().
    static void Stop() noexcept;
};

// ──────────────────────────────────────────────────────────────────────────
// RAII Scope — stack-only, zero heap. Reads Enabled() in ctor and skips QPC
// when disabled (the dominant runtime cost) so the macro is safe to leave in
// Release builds with VKEY_PERF_HIST=1 and `[debug] perf_histogram = false`.
// ──────────────────────────────────────────────────────────────────────────
struct Scope {
    using clock = std::chrono::steady_clock;

    Stage stage;
    clock::time_point t0;  // default-constructed (epoch) = "disabled at ctor"

    explicit Scope(Stage s) noexcept : stage(s) {
        if (Histogram::Enabled()) {
            t0 = clock::now();
        }
    }
    Scope(const Scope&)            = delete;
    Scope& operator=(const Scope&) = delete;

    ~Scope() noexcept {
        if (t0.time_since_epoch().count() == 0) return;  // gate was off at entry
        const auto dt = clock::now() - t0;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count();
        Histogram::Record(stage,
            ns > 0 ? static_cast<std::uint64_t>(ns) : 0ULL);
    }
};

// PERF_SCOPE(stage) — measure the lexical scope. When VKEY_PERF_HIST is
// undefined the macro compiles to nothing, so the call sites are free in
// builds where instrumentation is intentionally stripped.
#ifdef VKEY_PERF_HIST
  #define VKEY_PERF_SCOPE_CONCAT2(a, b) a##b
  #define VKEY_PERF_SCOPE_CONCAT(a, b)  VKEY_PERF_SCOPE_CONCAT2(a, b)
  #define PERF_SCOPE(stage_expr) \
      ::NextKey::Perf::Scope VKEY_PERF_SCOPE_CONCAT(_perf_scope_, __LINE__){(stage_expr)}
#else
  #define PERF_SCOPE(stage_expr) ((void)0)
#endif

}  // namespace NextKey::Perf
