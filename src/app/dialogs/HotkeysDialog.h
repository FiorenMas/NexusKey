// VKey - Unified Hotkey Rebind Dialog Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include "core/hotkey/HotkeyRegistry.h"

namespace NextKey {

/// Sub-dialog for rebinding the 3 NexusKey hotkeys (cancel-composition,
/// skip-macro, toggle-enabled). Reads/writes the `[[hotkeys]]` section of
/// config.toml via ConfigManager::LoadHotkeyRegistry / SaveHotkeyRegistry.
///
/// JS contract (hotkeys.js):
///   - Receives the current registry via `addTrigger(intent, label, vk, mods, double_tap)`
///   - Signals capture-mode events via `val-action` hidden input:
///       "add"     — append a captured trigger (vk/mods/double_tap in val-vk/val-mods/val-double-tap)
///       "delete"  — remove trigger (val-vk/val-mods/val-double-tap identify it)
///       "reset"   — restore factory defaults
///       "close"   — dismiss dialog
class HotkeysDialog : public SciterSubDialog {
public:
    HotkeysDialog(HWND parent);

    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

private:
    void populate();
    void persistAndSignal();
    void handleAction(const std::wstring& action);

    /// Upload the canonical VK→name table to JS as `[[vk, name], ...]` pairs.
    /// Called once after the document is ready so hotkeys.js capture preview
    /// doesn't have to duplicate the lookup table maintained in HotkeysDialog.cpp.
    void sendVkNames();

    /// Push per-intent enabled state to JS as `[[intent:string, enabled:bool], ...]`.
    /// Called from populate() so toggle visual state stays in sync with the registry.
    void sendEnabledStates();

    /// Read current val-intent / val-vk / val-mods / val-double-tap from DOM.
    /// Returns (intent, Trigger) parsed from the hidden inputs. `outValid` is
    /// false when any field is missing or out of range — caller must skip.
    Trigger readPendingTrigger(Intent& outIntent, bool& outValid);

    HotkeyRegistry registry_;
};

}  // namespace NextKey
