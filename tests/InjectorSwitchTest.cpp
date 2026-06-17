// InjectorSwitchTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Phase 4 — replay harness for concurrency invariants
// (docs/plans/2026-05-19-architecture-review-design.md §Phase 4).
//
// HookEngine's output strategy lives behind
//   std::atomic<std::shared_ptr<NextKey::Output::IOutputInjector>> injector_;
// (Sprint 2 T3). The publisher is `ApplyFocusOnHookThread` (hook thread —
// see Phase 2b); readers are the hot-path `HandleAlphaKey` /
// `ReplaceComposition` / `TryExpandMacro` dispatch paths. The interface
// is Linux-portable (output/IOutputInjector.h has no Win32 dependencies);
// implementations are Win32-only but irrelevant for the RCU contract.
//
// This suite pins the swap contract using fake injectors that record
// calls, so we can verify which physical impl actually ran each `Replace`
// / `SendKey` call:
//
//   1. Atomic round-trip: store + load returns the same impl.
//   2. Old injector stays alive while a reader holds it, even after the
//      writer publishes a new one — RCU pin semantics (no UAF).
//   3. Next-keystroke load picks up the new injector.
//   4. Mid-`Replace` swap: a reader that captured the injector before
//      the swap continues executing that injector's `Replace` to
//      completion; the swap does NOT cancel or redirect the in-flight
//      call. The next dispatch on the same thread reads the new impl.
//   5. Channel traits (`HasMultiProcessRenderer`, `NeedsBaitCharPrefix`,
//      `SettleBudget`) are sourced from the currently-loaded injector,
//      not from the type of the most-recently-stored pointer — a
//      regression here would mean HookEngine reads stale traits after a
//      focus change.
//
// Linux-portable: no Win32, no real injector impl, no HookEngine.

#include <gtest/gtest.h>

#include "app/output/IOutputInjector.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace NextKey {
namespace {

using ::NextKey::Output::IOutputInjector;

// ──────────────────────────────────────────────────────────────────────────
// Fake injector: records every Replace/SendKey call so tests can verify
// dispatch reached the expected impl, with the expected traits, in the
// expected order.
//
// `id` is the human-readable label appearing in failure messages; the
// numeric traits are configurable so each test can construct injectors
// that disagree on every trait (catches a "traits read from old impl
// after swap" regression).
// ──────────────────────────────────────────────────────────────────────────
struct FakeInjector final : IOutputInjector {
    std::string id;
    std::atomic<int> replaceCount{0};
    std::atomic<int> sendKeyCount{0};
    std::atomic<unsigned short> lastSendKeyVk{0};
    std::atomic<std::size_t> lastBsCount{0};
    std::wstring lastReplaceText;

    bool   multiProcessRenderer{false};
    bool   needsBaitChar{false};
    std::chrono::milliseconds settleBudget{100};

    // Test hook — set non-zero to simulate dispatch latency inside Replace.
    std::chrono::microseconds replaceDelay{0};

    explicit FakeInjector(std::string label) : id(std::move(label)) {}

    bool Replace(std::size_t bsCount, std::wstring_view text) noexcept override {
        replaceCount.fetch_add(1, std::memory_order_acq_rel);
        lastBsCount.store(bsCount, std::memory_order_release);
        // Note: this copy is fine for test scope; production
        // `Replace` impls don't copy because dispatch is to OS calls.
        lastReplaceText.assign(text.begin(), text.end());
        if (replaceDelay.count() > 0) {
            std::this_thread::sleep_for(replaceDelay);
        }
        return true;
    }
    void SendKey(unsigned short vkCode) noexcept override {
        sendKeyCount.fetch_add(1, std::memory_order_acq_rel);
        lastSendKeyVk.store(vkCode, std::memory_order_release);
    }
    [[nodiscard]] std::chrono::milliseconds SettleBudget() const noexcept override {
        return settleBudget;
    }
    [[nodiscard]] bool HasMultiProcessRenderer() const noexcept override {
        return multiProcessRenderer;
    }
    [[nodiscard]] bool NeedsBaitCharPrefix() const noexcept override {
        return needsBaitChar;
    }
};

// ──────────────────────────────────────────────────────────────────────────
// 1. Atomic round-trip.
// ──────────────────────────────────────────────────────────────────────────
TEST(InjectorSwitch, StoreLoadRoundTrip) {
    std::atomic<std::shared_ptr<IOutputInjector>> field;
    auto inj = std::make_shared<FakeInjector>("alpha");
    field.store(inj, std::memory_order_release);

    auto loaded = field.load(std::memory_order_acquire);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded.get(), inj.get())
        << "load must return the same pointer the store published";
    EXPECT_TRUE(loaded->Replace(0, L""));
    auto* fake = dynamic_cast<FakeInjector*>(loaded.get());
    ASSERT_NE(fake, nullptr);
    EXPECT_EQ(fake->replaceCount.load(), 1);
}

// ──────────────────────────────────────────────────────────────────────────
// 2. Old injector stays alive while reader holds it, even after writer
// publishes a new one and drops its own handle. Locks the "no UAF on
// focus change mid-keystroke" contract HookEngine implicitly relies on.
// ──────────────────────────────────────────────────────────────────────────
TEST(InjectorSwitch, OldInjectorSurvivesWhileReaderHolds) {
    std::atomic<std::shared_ptr<IOutputInjector>> field;
    auto alpha = std::make_shared<FakeInjector>("alpha");
    std::weak_ptr<IOutputInjector> weakAlpha = alpha;
    field.store(alpha, std::memory_order_release);

    // Reader captures alpha mid-dispatch.
    auto readerView = field.load(std::memory_order_acquire);

    // Writer publishes a new injector + drops its handle.
    auto beta = std::make_shared<FakeInjector>("beta");
    field.store(beta, std::memory_order_release);
    alpha.reset();
    beta.reset();

    // Reader can still call into alpha; the weak ref shouldn't be expired.
    EXPECT_FALSE(weakAlpha.expired())
        << "alpha must stay alive while readerView holds a strong ref, "
           "even though both writer locals were dropped";
    EXPECT_TRUE(readerView->Replace(2, L"x"));
    auto* fakeAlpha = dynamic_cast<FakeInjector*>(readerView.get());
    ASSERT_NE(fakeAlpha, nullptr);
    EXPECT_EQ(fakeAlpha->id, "alpha");

    // A fresh load from another reader sees beta.
    auto newReader = field.load(std::memory_order_acquire);
    auto* fakeBeta = dynamic_cast<FakeInjector*>(newReader.get());
    ASSERT_NE(fakeBeta, nullptr);
    EXPECT_EQ(fakeBeta->id, "beta");
    EXPECT_NE(readerView.get(), newReader.get());

    // Once readerView drops, alpha is reclaimable.
    readerView.reset();
    EXPECT_TRUE(weakAlpha.expired())
        << "alpha must die only after the last strong ref releases";
}

// ──────────────────────────────────────────────────────────────────────────
// 3. Next-keystroke load picks up the new injector. Locks the eventual-
// consistency direction: after a publish, subsequent loads ALWAYS see
// the latest impl (no caching staleness).
// ──────────────────────────────────────────────────────────────────────────
TEST(InjectorSwitch, NextDispatchUsesNewInjector) {
    std::atomic<std::shared_ptr<IOutputInjector>> field;
    auto alpha = std::make_shared<FakeInjector>("alpha");
    auto beta  = std::make_shared<FakeInjector>("beta");
    field.store(alpha, std::memory_order_release);

    // "Keystroke 1" — uses alpha.
    {
        auto inj = field.load(std::memory_order_acquire);
        EXPECT_TRUE(inj->Replace(1, L"a"));
    }
    EXPECT_EQ(alpha->replaceCount.load(), 1);
    EXPECT_EQ(beta->replaceCount.load(), 0);

    // Focus change publishes beta.
    field.store(beta, std::memory_order_release);

    // "Keystroke 2" — must use beta.
    {
        auto inj = field.load(std::memory_order_acquire);
        EXPECT_TRUE(inj->Replace(2, L"bc"));
    }
    EXPECT_EQ(alpha->replaceCount.load(), 1) << "alpha untouched on key 2";
    EXPECT_EQ(beta->replaceCount.load(), 1)  << "beta serves key 2";
    EXPECT_EQ(beta->lastBsCount.load(), 2u);
    EXPECT_EQ(beta->lastReplaceText, L"bc");
}

// ──────────────────────────────────────────────────────────────────────────
// 4. Mid-`Replace` swap: reader captures the injector, begins a slow
// `Replace`; writer swaps the field; reader's in-flight `Replace`
// finishes on the OLD impl (not redirected, not cancelled). Next
// dispatch on the same thread reads the new impl.
//
// This is the "focus change during ReplaceComposition mid-word" scenario
// HookEngine survives because the read captured `injector_.load()` BEFORE
// the swap — the local shared_ptr pins the old object until the call
// returns. The test enforces that exact ordering with a deliberate sleep
// inside `Replace`.
// ──────────────────────────────────────────────────────────────────────────
TEST(InjectorSwitch, MidReplaceSwapPreservesInFlightCall) {
    std::atomic<std::shared_ptr<IOutputInjector>> field;
    auto alpha = std::make_shared<FakeInjector>("alpha");
    auto beta  = std::make_shared<FakeInjector>("beta");
    alpha->replaceDelay = std::chrono::milliseconds(50);
    field.store(alpha, std::memory_order_release);

    std::atomic<bool> alphaCalled{false};
    std::atomic<bool> alphaReturned{false};

    std::thread reader([&] {
        // Load BEFORE the swap. shared_ptr local keeps alpha alive for
        // the whole call.
        auto inj = field.load(std::memory_order_acquire);
        alphaCalled.store(true, std::memory_order_release);
        EXPECT_TRUE(inj->Replace(3, L"abc"));
        alphaReturned.store(true, std::memory_order_release);
    });

    // Wait until the reader is inside Replace, then swap.
    while (!alphaCalled.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    field.store(beta, std::memory_order_release);

    // Brief pause — alpha is still mid-Replace.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(alphaReturned.load(std::memory_order_acquire))
        << "alpha's slow Replace should still be in progress at this point";
    EXPECT_EQ(beta->replaceCount.load(), 0)
        << "beta must NOT have been called — the swap doesn't redirect "
           "an in-flight reader's pinned shared_ptr";

    reader.join();
    EXPECT_TRUE(alphaReturned.load());
    EXPECT_EQ(alpha->replaceCount.load(), 1);
    EXPECT_EQ(alpha->lastBsCount.load(), 3u);
    EXPECT_EQ(alpha->lastReplaceText, L"abc");
    EXPECT_EQ(beta->replaceCount.load(), 0);

    // Next keystroke (post-swap, post-reader-exit) hits beta.
    auto next = field.load(std::memory_order_acquire);
    EXPECT_TRUE(next->Replace(1, L"d"));
    EXPECT_EQ(beta->replaceCount.load(), 1);
    EXPECT_EQ(alpha->replaceCount.load(), 1);
}

// ──────────────────────────────────────────────────────────────────────────
// 5. Channel traits come from the currently-loaded impl, not a cached
// snapshot of the most-recently-stored type. HookEngine reads
// `injector_.load()->NeedsBaitCharPrefix()` etc. per dispatch; the trait
// answer must change when a new injector is published.
// ──────────────────────────────────────────────────────────────────────────
TEST(InjectorSwitch, TraitsTrackTheActiveInjector) {
    std::atomic<std::shared_ptr<IOutputInjector>> field;

    auto win32 = std::make_shared<FakeInjector>("win32-batch");
    win32->multiProcessRenderer = false;
    win32->needsBaitChar        = false;
    win32->settleBudget         = std::chrono::milliseconds(30);

    auto split = std::make_shared<FakeInjector>("split-electron");
    split->multiProcessRenderer = true;
    split->needsBaitChar        = false;
    split->settleBudget         = std::chrono::milliseconds(100);

    auto chrome = std::make_shared<FakeInjector>("chrome-bait");
    chrome->multiProcessRenderer = true;
    chrome->needsBaitChar        = true;
    chrome->settleBudget         = std::chrono::milliseconds(60);

    field.store(win32, std::memory_order_release);
    {
        auto inj = field.load(std::memory_order_acquire);
        EXPECT_FALSE(inj->HasMultiProcessRenderer());
        EXPECT_FALSE(inj->NeedsBaitCharPrefix());
        EXPECT_EQ(inj->SettleBudget(), std::chrono::milliseconds(30));
    }

    field.store(split, std::memory_order_release);
    {
        auto inj = field.load(std::memory_order_acquire);
        EXPECT_TRUE(inj->HasMultiProcessRenderer());
        EXPECT_FALSE(inj->NeedsBaitCharPrefix());
        EXPECT_EQ(inj->SettleBudget(), std::chrono::milliseconds(100));
    }

    field.store(chrome, std::memory_order_release);
    {
        auto inj = field.load(std::memory_order_acquire);
        EXPECT_TRUE(inj->HasMultiProcessRenderer());
        EXPECT_TRUE(inj->NeedsBaitCharPrefix());
        EXPECT_EQ(inj->SettleBudget(), std::chrono::milliseconds(60));
    }
}

}  // namespace
}  // namespace NextKey
