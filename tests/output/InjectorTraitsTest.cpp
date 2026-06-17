// tests/output/InjectorTraitsTest.cpp
//
// Unit tests for IOutputInjector channel-trait queries (post-T3 follow-up:
// "ChannelTraits + isElectronApp_ / needBaitChar_ deletion" per docs/TODO.md).
//
// HookEngine used to maintain duplicate atomic flags (isElectronApp_,
// needBaitChar_) that mirrored the dispatch channel's character. After
// this refactor the source of truth lives on the injector itself and
// HookEngine reads via std::atomic_load(&injector_)->trait method,
// removing the two duplicated atomic stores from OnFocusChanged and
// the two atomic loads from HandleAlphaKey.
//
// Each test asserts the trait value an injector reports given its
// constructor arguments. Factory tests assert the propagation path:
// WindowClassification → Create() → constructed injector → trait query.
//
// Spec: docs/CODING_RULES (Rule #11 / Pillar #4: "Mở rộng không ảnh
// hưởng perf" — channel-specific behaviour belongs with the channel,
// not duplicated in HookEngine).
#include "app/output/IOutputInjector.h"
#include "app/output/Win32SendInputInjector.h"
#include "app/output/SplitDispatchInjector.h"
#include "app/output/RichEditEmReplaceSelInjector.h"
#include "app/output/OutputInjectorFactory.h"

#include <gtest/gtest.h>

namespace NextKey::Output::Test {

// ─── Win32SendInputInjector ──────────────────────────────────────────

TEST(InjectorTraitsTest, Win32_NoBaitFlag_NeedsBaitCharPrefixIsFalse) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/false);
    EXPECT_FALSE(inj.NeedsBaitCharPrefix());
}

TEST(InjectorTraitsTest, Win32_BaitFlag_NeedsBaitCharPrefixIsTrue) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.NeedsBaitCharPrefix());
}

TEST(InjectorTraitsTest, Win32_HasMultiProcessRendererAlwaysFalse) {
    // Win32 batched dispatch is for vanilla single-process renderers.
    // The bait flag controls Chromium suggest-dismiss but the Chromium
    // browser process model itself doesn't change Win32's
    // single-channel synth/physical ordering.
    Win32SendInputInjector noBait(false);
    Win32SendInputInjector withBait(true);
    EXPECT_FALSE(noBait.HasMultiProcessRenderer());
    EXPECT_FALSE(withBait.HasMultiProcessRenderer());
}

// ─── SplitDispatchInjector ───────────────────────────────────────────

TEST(InjectorTraitsTest, Split_Defaults_BothTraitsFalse) {
    // Console-style construction (sleepMs only). No bait, no multi-proc.
    SplitDispatchInjector inj(/*sleepMsBetweenBatches=*/5);
    EXPECT_FALSE(inj.NeedsBaitCharPrefix());
    EXPECT_FALSE(inj.HasMultiProcessRenderer());
}

TEST(InjectorTraitsTest, Split_BaitOnly_BaitTrueMultiProcFalse) {
    SplitDispatchInjector inj(/*sleepMs=*/6,
                              /*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.NeedsBaitCharPrefix());
    EXPECT_FALSE(inj.HasMultiProcessRenderer());
}

TEST(InjectorTraitsTest, Split_MultiProcOnly_BaitFalseMultiProcTrue) {
    SplitDispatchInjector inj(/*sleepMs=*/6,
                              /*needsBaitCharPrefix=*/false,
                              /*hasMultiProcessRenderer=*/true);
    EXPECT_FALSE(inj.NeedsBaitCharPrefix());
    EXPECT_TRUE(inj.HasMultiProcessRenderer());
}

TEST(InjectorTraitsTest, Split_BothFlags_BothTraitsTrue) {
    // Electron-on-Chromium (Discord/Slack/VSCode/WebView2) — split
    // dispatch + Chromium suggest dismiss + multi-process renderer race.
    SplitDispatchInjector inj(/*sleepMs=*/6,
                              /*needsBaitCharPrefix=*/true,
                              /*hasMultiProcessRenderer=*/true);
    EXPECT_TRUE(inj.NeedsBaitCharPrefix());
    EXPECT_TRUE(inj.HasMultiProcessRenderer());
}

// ─── RichEditEmReplaceSelInjector ────────────────────────────────────

TEST(InjectorTraitsTest, RichEdit_BothTraitsFalseByDefault) {
    // Win11 New Notepad RichEditD2DPT — sent-message replacement,
    // single-process renderer, no Chromium suggest layer.
    RichEditEmReplaceSelInjector inj;
    EXPECT_FALSE(inj.NeedsBaitCharPrefix());
    EXPECT_FALSE(inj.HasMultiProcessRenderer());
}

// ─── Factory propagation ─────────────────────────────────────────────

TEST(InjectorTraitsTest, Factory_DefaultClassification_NoTraits) {
    WindowClassification c{};
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_FALSE(inj->NeedsBaitCharPrefix());
    EXPECT_FALSE(inj->HasMultiProcessRenderer());
}

TEST(InjectorTraitsTest, Factory_Chromium_BaitTrueOnWin32) {
    WindowClassification c{};
    c.isChromium = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_TRUE(inj->NeedsBaitCharPrefix())
        << "isChromium classification must propagate as bait-prefix trait";
    EXPECT_FALSE(inj->HasMultiProcessRenderer())
        << "vanilla Chromium browsers stay on Win32 single channel";
}

TEST(InjectorTraitsTest, Factory_Electron_MultiProcTrueBaitFalseByDefault) {
    WindowClassification c{};
    c.isElectron = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_FALSE(inj->NeedsBaitCharPrefix())
        << "non-Chromium Electron variants don't need the bait prefix";
    EXPECT_TRUE(inj->HasMultiProcessRenderer())
        << "Electron classification must propagate as multi-process trait";
}

TEST(InjectorTraitsTest, Factory_ElectronChromium_BothTraitsTrue) {
    WindowClassification c{};
    c.isElectron = true;
    c.isChromium = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_TRUE(inj->NeedsBaitCharPrefix());
    EXPECT_TRUE(inj->HasMultiProcessRenderer());
}

TEST(InjectorTraitsTest, Factory_Console_BothTraitsFalse) {
    // CMD / PowerShell — split dispatch (5 ms) for ingest pacing, but
    // single-process renderer and no Chromium suggest layer.
    WindowClassification c{};
    c.isConsole = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_FALSE(inj->NeedsBaitCharPrefix());
    EXPECT_FALSE(inj->HasMultiProcessRenderer())
        << "Console hosts use split-dispatch but are NOT multi-process renderers";
}

TEST(InjectorTraitsTest, Factory_RichEdit_BothTraitsFalse) {
    WindowClassification c{};
    c.isRichEditD2DPT = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_FALSE(inj->NeedsBaitCharPrefix());
    EXPECT_FALSE(inj->HasMultiProcessRenderer());
}

}  // namespace NextKey::Output::Test
