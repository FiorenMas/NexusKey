// VKey - Hotkey Wiring Helper
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "HotkeyManager.h"
#include <memory>

namespace NextKey {

class HookEngine;
class TrayIcon;
class QuickConvert;
struct HotkeyConfig;

/// Wire toggle and quick-convert hotkeys. Creates QuickConvert, sets config
/// reload callback on hookEngine, registers hotkeys, and initializes manager.
/// Call once at startup after TrayIcon is created.
///
/// @param quickConvert    Output: created QuickConvert instance
/// @param outToggleSlot   Output: slot ID for toggle hotkey (captured by reload callback)
/// @param outConvertSlot  Output: slot ID for convert hotkey (captured by reload callback)
void WireHotkeys(
    HotkeyManager& hotkeyManager,
    HookEngine& hookEngine,
    TrayIcon& trayIcon,
    std::unique_ptr<QuickConvert>& quickConvert,
    HotkeyManager::SlotId& outToggleSlot,
    HotkeyManager::SlotId& outConvertSlot,
    HINSTANCE hInstance,
    const HotkeyConfig& toggleConfig
);

}  // namespace NextKey
