// VKey - Window Picker Base Dialog
// SPDX-License-Identifier: AGPL-3.0-only
//
// Base class for app-list dialogs that support window picking and
// running-app enumeration. Eliminates duplicate code across
// ExcludedAppsDialog, TsfAppsDialog, and AppOverridesDialog.

#pragma once

#include "SciterSubDialog.h"
#include <string>
#include <vector>

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif

namespace NextKey {

/// Base class for dialogs that let users pick windows or enumerate running apps.
/// Provides: system-cursor crosshair picking, exe-name lookup, process enumeration.
/// Subclasses must implement onWindowPicked() to handle the picked exe name.
class WindowPickerDialog : public SciterSubDialog {
public:
    using SciterSubDialog::SciterSubDialog;

protected:
    /// Called after stopWindowPicking() when the user clicks a window.
    /// exeName is lowercase. The cursor is already restored when this is called.
    virtual void onWindowPicked(const std::wstring& exeName) = 0;

    /// Enumerate non-system running processes (sorted, deduplicated). O(n log n).
    [[nodiscard]] static std::vector<std::wstring> getRunningApps();

    /// Get the lowercase exe name from a window handle. Returns "" on failure.
    [[nodiscard]] static std::wstring getExeNameFromWindow(HWND hwnd);

    /// Begin crosshair window picking (captures mouse, replaces system cursor).
    void startWindowPicking();

    /// End picking and restore cursor. Always safe to call even if not picking.
    void stopWindowPicking();

    /// Handles WM_SETCURSOR, WM_LBUTTONUP, WM_KEYDOWN (ESC) for the picker.
    LRESULT onCustomMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

    bool isPickingWindow_ = false;
    HCURSOR savedArrowCursor_ = nullptr;
};

}  // namespace NextKey
