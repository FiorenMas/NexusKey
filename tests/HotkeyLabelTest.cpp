// VKey — HotkeyLabel formatter unit tests (Linux-portable, no Win32 deps).
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>

#include <string>

#include "core/hotkey/HotkeyLabel.h"
#include "core/hotkey/HotkeyRegistry.h"  // kModCtrl / kModShift / kModAlt / kModWin


namespace NextKey {
namespace {

// VK literals (Linux-portable copies of Win32 constants).
constexpr uint32_t kVkZ     = 0x5A;
constexpr uint32_t kVkF1    = 0x70;
constexpr uint32_t kVkF5    = 0x74;
constexpr uint32_t kVkF12   = 0x7B;
constexpr uint32_t kVk0     = 0x30;
constexpr uint32_t kVk9     = 0x39;
constexpr uint32_t kVkEsc   = 0x1B;
constexpr uint32_t kVkSpace = 0x20;

// ───────────────────────────── Empty / unset ─────────────────────────────

TEST(HotkeyLabel, EmptyWhenNothingAssigned) {
    EXPECT_EQ(FormatHotkeyLabel(0, 0), L"");
}

TEST(HotkeyLabel, ModifiersOnlyWhenVkZero) {
    EXPECT_EQ(FormatHotkeyLabel(0, kModCtrl), L"Ctrl");
    EXPECT_EQ(FormatHotkeyLabel(0, kModCtrl | kModShift), L"Ctrl+Shift");
    EXPECT_EQ(FormatHotkeyLabel(0, kModCtrl | kModShift | kModAlt | kModWin),
              L"Ctrl+Shift+Alt+Win");
}

// ───────────────────────────── Letters / digits ──────────────────────────

TEST(HotkeyLabel, AltZ) {
    EXPECT_EQ(FormatHotkeyLabel(kVkZ, kModAlt), L"Alt+Z");
}

TEST(HotkeyLabel, JustZ) {
    EXPECT_EQ(FormatHotkeyLabel(kVkZ, 0), L"Z");
}

TEST(HotkeyLabel, CtrlShift0) {
    EXPECT_EQ(FormatHotkeyLabel(kVk0, kModCtrl | kModShift), L"Ctrl+Shift+0");
}

TEST(HotkeyLabel, CtrlShift9) {
    EXPECT_EQ(FormatHotkeyLabel(kVk9, kModCtrl | kModShift), L"Ctrl+Shift+9");
}

// ───────────────────────────── F-row ─────────────────────────────────────

TEST(HotkeyLabel, CtrlShiftF1) {
    EXPECT_EQ(FormatHotkeyLabel(kVkF1, kModCtrl | kModShift),
              L"Ctrl+Shift+F1");
}

TEST(HotkeyLabel, F5Alone) {
    EXPECT_EQ(FormatHotkeyLabel(kVkF5, 0), L"F5");
}

TEST(HotkeyLabel, F12) {
    EXPECT_EQ(FormatHotkeyLabel(kVkF12, kModAlt), L"Alt+F12");
}

// ───────────────────────────── Named keys ────────────────────────────────

TEST(HotkeyLabel, EscNamed) {
    EXPECT_EQ(FormatHotkeyLabel(kVkEsc, 0), L"Esc");
}

TEST(HotkeyLabel, SpaceNamed) {
    EXPECT_EQ(FormatHotkeyLabel(kVkSpace, kModCtrl), L"Ctrl+Space");
}

// ───────────────────────────── Numpad digits ─────────────────────────────
// Numpad keys (VK 0x60..0x69) render as "Num0".."Num9" via the algorithmic
// branch in VkToKeyName. Pre-2026-05-21 these fell through to the hex
// fallback ("VK 0x0060"); this test pins the friendly rendering so a
// careless refactor of the table doesn't silently regress it.

TEST(HotkeyLabel, NumpadZero) {
    EXPECT_EQ(FormatHotkeyLabel(0x60, 0), L"Num0");
}

TEST(HotkeyLabel, NumpadNineWithCtrl) {
    EXPECT_EQ(FormatHotkeyLabel(0x69, kModCtrl), L"Ctrl+Num9");
}

TEST(HotkeyLabel, NumpadFiveAltShift) {
    EXPECT_EQ(FormatHotkeyLabel(0x65, kModShift | kModAlt), L"Shift+Alt+Num5");
}

// ───────────────────────────── Style variants ────────────────────────────
// GetVkDisplayNames(VkNameStyle) exposes the canonical irregular-VK table
// for UI layers that need different rendering — words on wide buttons vs
// compact arrows in tight HotkeysDialog chips.

TEST(HotkeyLabel, StyleWordsHasArrowsAsWords) {
    bool found = false;
    for (const auto& [vk, name] : GetVkDisplayNames(VkNameStyle::Words)) {
        if (vk == 0x25) { EXPECT_STREQ(name, L"Left");  found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST(HotkeyLabel, StyleCompactArrowsHasArrowsAsGlyphs) {
    bool found = false;
    for (const auto& [vk, name] : GetVkDisplayNames(VkNameStyle::CompactArrows)) {
        if (vk == 0x25) { EXPECT_STREQ(name, L"←"); found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST(HotkeyLabel, StyleVariantsAgreeOnSharedEntries) {
    // Space, Esc, Backspace etc. must be identical across styles.
    const auto& words   = GetVkDisplayNames(VkNameStyle::Words);
    const auto& arrows  = GetVkDisplayNames(VkNameStyle::CompactArrows);
    ASSERT_EQ(words.size(), arrows.size());
    for (const auto& [vk, name] : words) {
        if (vk == 0x25 || vk == 0x26 || vk == 0x27 || vk == 0x28 || vk == 0x2E) {
            continue;  // arrows + Delete intentionally diverge
        }
        bool foundMatch = false;
        for (const auto& [vk2, name2] : arrows) {
            if (vk == vk2) {
                EXPECT_STREQ(name, name2) << "vk=0x" << std::hex << vk;
                foundMatch = true;
                break;
            }
        }
        EXPECT_TRUE(foundMatch) << "vk=0x" << std::hex << vk << " missing from arrows table";
    }
}

// ───────────────────────────── Modifier-as-key labels ───────────────────
// When vk IS a modifier (e.g., Ctrl+Shift chord that captured the Shift
// release as the trigger key), the modifier needs its own friendly name —
// otherwise the fallback prints "VK 0x0010" which looks broken.

TEST(HotkeyLabel, ModifierAsKeyShift) {
    EXPECT_EQ(FormatHotkeyLabel(0x10, kModCtrl), L"Ctrl+Shift");
}

TEST(HotkeyLabel, ModifierAsKeyCtrl) {
    EXPECT_EQ(FormatHotkeyLabel(0x11, kModShift), L"Shift+Ctrl");
}

TEST(HotkeyLabel, ModifierAsKeyAlt) {
    EXPECT_EQ(FormatHotkeyLabel(0x12, kModCtrl), L"Ctrl+Alt");
}

TEST(HotkeyLabel, ModifierAsKeyLWinAlone) {
    EXPECT_EQ(FormatHotkeyLabel(0x5B, 0), L"Win");
}

TEST(HotkeyLabel, ModifierAsKeyRWinAlone) {
    EXPECT_EQ(FormatHotkeyLabel(0x5C, 0), L"Win");
}

// ───────────────────────────── Modifier order is stable ──────────────────

TEST(HotkeyLabel, ModifierOrderIsCtrlShiftAltWin) {
    // Regardless of bit order in the mask, output is Ctrl→Shift→Alt→Win.
    EXPECT_EQ(FormatHotkeyLabel(kVkZ, kModWin | kModAlt | kModShift | kModCtrl),
              L"Ctrl+Shift+Alt+Win+Z");
}

}  // namespace
}  // namespace NextKey
