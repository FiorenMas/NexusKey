// VKey — Hotkey label formatter. Linux-portable: VK codes are plain integers.
// Single source of truth for VK+mods → "Ctrl+Shift+F5" strings, shared between
// TrayIcon binding text, Sciter dialog C++ side, and the Classic Win32 UI.
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace NextKey {

/// Render style for the irregular-VK table.
///   - Words:         "Left", "Up", "Right", "Down", "Delete" — fits buttons
///                    and the Classic record-button text.
///   - CompactArrows: "←", "↑", "→", "↓", "Del" — fits HotkeysDialog chips
///                    where width is tight.
/// Other entries (Space, Backspace, Esc, OEM punct…) are identical across
/// both styles — only the diverging entries above care about the choice.
enum class VkNameStyle { Words, CompactArrows };

/// Canonical VK → display-name table for irregular keys (Space, Backspace,
/// Esc, OEM punctuation, etc.). Letters/digits/F-keys/numpad are handled
/// algorithmically by FormatHotkeyLabel and don't appear here.
///
/// Single source of truth, shared between FormatHotkeyLabel (C++) and the
/// Sciter UI (uploaded via `setVkNames` JS helper).
[[nodiscard]] const std::vector<std::pair<uint32_t, const wchar_t*>>&
GetVkDisplayNames(VkNameStyle style = VkNameStyle::Words);

/// Render a hotkey combination as a human-readable wide string. Modifier
/// order is always Ctrl → Shift → Alt → Win, regardless of `mods` bit order.
///
/// Returns:
///   - "" when `vk == 0 && mods == 0` (nothing assigned).
///   - "Ctrl+Shift" etc. when `vk == 0` but modifiers are set
///     (modifier-only triggers — registry features, not convert-tool).
///   - "Ctrl+Shift+F5", "Alt+Z", "Space" etc. for the general case.
///
/// `vk` is interpreted with Win32 VK_* semantics:
///   - 0x30..0x39 → "0".."9"
///   - 0x41..0x5A → "A".."Z"
///   - 0x70..0x87 → "F1".."F24"
///   - 0x1B → "Esc"
///   - 0x20 → "Space"
///   - 0x08 → "Backspace"
///   - 0x09 → "Tab"
///   - 0x0D → "Enter"
///   - other → "VK 0x..." (hex fallback, so the user still sees something).
///
/// `mods` uses the `kMod*` bitmask from HotkeyRegistry.h
/// (`kModCtrl | kModShift | kModAlt | kModWin`).
[[nodiscard]] std::wstring FormatHotkeyLabel(uint32_t vk, uint32_t mods);

/// Migration helper — map a legacy `HotkeyConfig::key` (single wide char,
/// pre-2026-05 schema) to a Win32 VK code. Clean rule, no guessing:
///   - "A".."Z" / "a".."z" → 0x41..0x5A (case-folded to uppercase)
///   - "0".."9"            → 0x30..0x39
///   - everything else     → 0 (user must rebind via new capture overlay)
///
/// Returns 0 for empty / multi-character / OEM punctuation input. Used by
/// ConfigManager::LoadConvertConfig when reading pre-migration TOML.
[[nodiscard]] uint32_t LegacyKeyCharToVk(const std::wstring& s) noexcept;

}  // namespace NextKey
