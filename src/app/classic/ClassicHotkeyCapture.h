// VKey Classic — Shared hotkey capture modal. Used by ClassicHotkeysDialog
// (full feature: double-tap, modifier-alone) and ClassicConvertToolDialog
// (combo-only: a chord that includes a non-modifier key).
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include <Windows.h>
#include <cstdint>
#include <optional>

namespace NextKey::Classic {

struct HotkeyCaptureResult {
    uint32_t vk        = 0;  // Win32 VK_*; canonicalised (LCONTROL → CONTROL etc.)
    uint32_t mods      = 0;  // kModCtrl | kModShift | kModAlt | kModWin
    bool     doubleTap = false;  // only set when options.allowDoubleTap == true
};

struct HotkeyCaptureOptions {
    /// Allow "2×Alt"-style gestures. HotkeysDialog: true. ConvertTool: false
    /// (the HotkeyManager runtime slot for convert can't match double-tap).
    bool allowDoubleTap = true;

    /// Allow "Ctrl alone" / "Ctrl+Shift" modifier-only triggers. HotkeysDialog:
    /// true. ConvertTool: false — convert hotkey requires a non-modifier key.
    bool allowBareModifier = true;

    /// Optional UI overrides — if null, uses the default Vietnamese strings.
    const wchar_t* titleText  = nullptr;
    const wchar_t* promptText = nullptr;
};

/// Show the modal capture dialog. Blocks until the user commits a binding
/// or cancels (Esc / close button / WM_CLOSE). Disables `parent` for the
/// duration. Returns nullopt on cancel.
///
/// Note: bare-Escape (no modifiers held) always cancels — Esc cannot be
/// bound as a standalone hotkey via this dialog regardless of the options.
/// Esc+modifier (e.g. Ctrl+Esc) IS captured as a normal chord.
std::optional<HotkeyCaptureResult> ShowHotkeyCaptureDialog(
    HINSTANCE hInstance, HWND parent, ClassicTheme& theme, UINT dpi,
    const HotkeyCaptureOptions& opts = {});

}  // namespace NextKey::Classic

#endif
