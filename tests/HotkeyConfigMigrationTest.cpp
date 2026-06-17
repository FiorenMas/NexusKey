// VKey — HotkeyConfig schema migration (wchar_t key → uint32_t vk).
// Verifies the legacy-character → VK rule: clean (A-Z/0-9), nothing else.
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include <string>

#include "core/config/TypingConfig.h"
#include "core/hotkey/HotkeyLabel.h"
#include "core/hotkey/HotkeyRegistry.h"  // kModCtrl/Shift/Alt/Win

namespace NextKey {
namespace {

// ───────────────────────────── LegacyKeyCharToVk ─────────────────────────

TEST(LegacyKeyCharToVk, UppercaseLetterMapsToVk) {
    EXPECT_EQ(LegacyKeyCharToVk(L"Z"), 0x5Au);
    EXPECT_EQ(LegacyKeyCharToVk(L"A"), 0x41u);
    EXPECT_EQ(LegacyKeyCharToVk(L"M"), 0x4Du);
}

TEST(LegacyKeyCharToVk, LowercaseCaseFoldsToUpper) {
    EXPECT_EQ(LegacyKeyCharToVk(L"z"), 0x5Au);
    EXPECT_EQ(LegacyKeyCharToVk(L"a"), 0x41u);
}

TEST(LegacyKeyCharToVk, DigitMapsToVk) {
    EXPECT_EQ(LegacyKeyCharToVk(L"0"), 0x30u);
    EXPECT_EQ(LegacyKeyCharToVk(L"5"), 0x35u);
    EXPECT_EQ(LegacyKeyCharToVk(L"9"), 0x39u);
}

TEST(LegacyKeyCharToVk, EmptyStringReturnsZero) {
    EXPECT_EQ(LegacyKeyCharToVk(L""), 0u);
}

TEST(LegacyKeyCharToVk, MultiCharRejected) {
    // Old config sometimes had stray strings like "ZZ" or "Alt+Z";
    // we reject rather than guess. User must rebind via new capture overlay.
    EXPECT_EQ(LegacyKeyCharToVk(L"ZZ"), 0u);
    EXPECT_EQ(LegacyKeyCharToVk(L"Alt+Z"), 0u);
    EXPECT_EQ(LegacyKeyCharToVk(L"F1"), 0u);   // not a single character
}

TEST(LegacyKeyCharToVk, OemAndPunctRejected) {
    // ~, `, -, etc — no guessing of OEM_* codes; user reassigns.
    EXPECT_EQ(LegacyKeyCharToVk(L"~"), 0u);
    EXPECT_EQ(LegacyKeyCharToVk(L"`"), 0u);
    EXPECT_EQ(LegacyKeyCharToVk(L"-"), 0u);
    EXPECT_EQ(LegacyKeyCharToVk(L" "), 0u);
    EXPECT_EQ(LegacyKeyCharToVk(L"."), 0u);
}

// ───────────────────────────── HotkeyConfig::ToMods ──────────────────────

TEST(HotkeyConfigToMods, AllFlagsOff) {
    HotkeyConfig cfg{};
    EXPECT_EQ(cfg.ToMods(), 0u);
}

TEST(HotkeyConfigToMods, CtrlOnly) {
    HotkeyConfig cfg{};
    cfg.ctrl = true;
    EXPECT_EQ(cfg.ToMods(), kModCtrl);
}

TEST(HotkeyConfigToMods, AllFour) {
    HotkeyConfig cfg{.ctrl = true, .shift = true, .alt = true, .win = true};
    EXPECT_EQ(cfg.ToMods(), kModCtrl | kModShift | kModAlt | kModWin);
}

// ───────────────────────────── HotkeyConfig::SetModsFromMask ─────────────

TEST(HotkeyConfigSetModsFromMask, EmptyMaskClearsAllFlags) {
    HotkeyConfig cfg{.ctrl = true, .shift = true, .alt = true, .win = true,
                     .vk = 0x5A};
    cfg.SetModsFromMask(0);
    EXPECT_FALSE(cfg.ctrl);
    EXPECT_FALSE(cfg.shift);
    EXPECT_FALSE(cfg.alt);
    EXPECT_FALSE(cfg.win);
    EXPECT_EQ(cfg.vk, 0x5Au) << "vk must NOT be touched by SetModsFromMask";
}

TEST(HotkeyConfigSetModsFromMask, CtrlShiftMask) {
    HotkeyConfig cfg{};
    cfg.SetModsFromMask(kModCtrl | kModShift);
    EXPECT_TRUE(cfg.ctrl);
    EXPECT_TRUE(cfg.shift);
    EXPECT_FALSE(cfg.alt);
    EXPECT_FALSE(cfg.win);
}

TEST(HotkeyConfigSetModsFromMask, RoundTripWithToMods) {
    // Setting then re-extracting the mask must be a no-op for every subset.
    for (uint32_t mask = 0; mask <= 0x0F; ++mask) {
        HotkeyConfig cfg{};
        cfg.SetModsFromMask(mask);
        EXPECT_EQ(cfg.ToMods(), mask) << "mask=" << mask;
    }
}

// ───────────────────────────── HasAny / ModifiersMatch after rename ──────

TEST(HotkeyConfig, HasAnyTrueWhenVkSet) {
    HotkeyConfig cfg{};
    cfg.vk = 0x5A;
    EXPECT_TRUE(cfg.HasAny());
}

TEST(HotkeyConfig, HasAnyTrueWhenModifierSet) {
    HotkeyConfig cfg{};
    cfg.alt = true;
    EXPECT_TRUE(cfg.HasAny());
}

TEST(HotkeyConfig, HasAnyFalseOnEmpty) {
    HotkeyConfig cfg{};
    EXPECT_FALSE(cfg.HasAny());
}

TEST(HotkeyConfig, ModifiersMatchAfterRename) {
    HotkeyConfig cfg{.alt = true, .vk = 0x5A};
    EXPECT_TRUE(cfg.ModifiersMatch(false, false, true, false));
    EXPECT_FALSE(cfg.ModifiersMatch(true,  false, true, false));  // extra Ctrl
    EXPECT_FALSE(cfg.ModifiersMatch(false, false, false, false));  // no Alt
}

}  // namespace
}  // namespace NextKey
