// VKey Classic — Unified Hotkey Rebind Dialog
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32
#include "ClassicTheme.h"
#include "ClassicDialogUtils.h"
#include "core/hotkey/HotkeyRegistry.h"
#include <Windows.h>
#include <commctrl.h>
#include <array>
#include <string>

namespace NextKey::Classic {

/// Win32 native parity of Sciter's HotkeysDialog. Three sections — one per
/// Intent — each with a per-intent enable checkbox, a chip ListView of
/// bound triggers, and Add/Reset buttons. Add launches a modal capture
/// overlay that records the next key (or double-tap of a modifier).
///
/// Persists to `[[hotkeys]]` + `[hotkey_state]` via ConfigManager and
/// notifies the running HookEngine via SignalConfigChange().
class ClassicHotkeysDialog {
public:
    /// Show modal dialog. Returns true if the registry was modified.
    static bool Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme = false);

private:
    ClassicHotkeysDialog() = default;

    bool Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme);
    void CreateControls();
    void RepopulateAll();
    void RepopulateSection(Intent intent);

    void OnAddClicked(Intent intent);
    void OnResetClicked(Intent intent);
    void OnDeleteSelected(Intent intent);
    void OnEnableToggled(Intent intent, bool enabled);
    void OnCloseClicked();

    void PersistAndSignal();

    /// Modal capture overlay — blocks until the user presses a key (or Esc to
    /// cancel). On success writes the captured trigger into `outTrigger` and
    /// returns true.
    bool CaptureTrigger(Trigger& outTrigger);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    int Dpi(int value) const noexcept;

    // -- Layout constants (96 DPI baseline) --
    static constexpr int kWidth        = 460;
    static constexpr int kHeight       = 540;
    static constexpr int kPadding      = 14;
    static constexpr int kSectionGap   = 10;
    static constexpr int kSectionH     = 148;
    static constexpr int kHeaderH      = 22;
    static constexpr int kListH        = 80;
    static constexpr int kBtnH         = 26;
    static constexpr int kCheckW       = 80;
    static constexpr int kAddW         = 80;
    static constexpr int kDeleteW      = 70;
    static constexpr int kResetW       = 90;
    static constexpr int kCloseW       = 80;

    static constexpr const wchar_t* kClassName = L"VKeyClassicHotkeys";

    // -- Per-section UI handles --
    struct SectionUI {
        HWND label     = nullptr;
        HWND enable    = nullptr;
        HWND listView  = nullptr;
        HWND btnAdd    = nullptr;
        HWND btnDelete = nullptr;
        HWND btnReset  = nullptr;
    };

    // -- State --
    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    ClassicTheme theme_;
    UINT dpi_ = 96;
    bool modified_ = false;

    HotkeyRegistry registry_;
    std::array<SectionUI, 3> sections_{};   // indexed by Intent value
    HWND btnClose_ = nullptr;
};

}  // namespace NextKey::Classic

#endif  // _WIN32
