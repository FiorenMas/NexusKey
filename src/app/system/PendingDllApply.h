// VKey - Apply deferred TSF DLL swap at EXE startup
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <Windows.h>

namespace NextKey {

class SharedStateManager;

/// Result of ApplyPendingDllUpdate() — drives the restart banner.
enum class PendingDllState {
    None = 0,                 // Nothing pending; normal startup.
    SwapFailed = 1,           // Pending file exists but live DLL could not be released.
    SwapDoneNeedsReboot = 2   // New DLL on disk; other processes may still hold the old one.
};

/// Called early in the main-process startup path (before CleanupOldUpdateFiles).
/// Inspects `exeDir\\VKeyTSF.dll.pending`; if present, tries to swap it in
/// for the live copy. Never throws. Caller MUST publish the result into
/// SharedState flags (TSF_PENDING_DLL_SWAP / TSF_POST_UPDATE_REBOOT) so
/// subprocess dialogs can observe it — hence [[nodiscard]].
[[nodiscard]] PendingDllState ApplyPendingDllUpdate() noexcept;

/// Prompt for reboot (localized via S(UPDATE_BANNER_CONFIRM)) and, on OK,
/// call RestartWindowsNow(). Used by SettingsDialog (Sciter) and TrayIcon.
/// Classic dialog prefers RestartWindowsNow() directly — it already shows its
/// own banner-copy MessageBox and doesn't need a second confirmation.
void RestartWindowsWithPrompt(HWND owner) noexcept;

/// Acquire SE_SHUTDOWN_NAME privilege and call
/// ExitWindowsEx(EWX_REBOOT | EWX_RESTARTAPPS). No UI. Callers are expected to
/// have already obtained user consent.
void RestartWindowsNow() noexcept;

/// Banner to display on the restart banner in Settings / tray.
enum class UpdateBannerState {
    Hide = 0,           // No banner.
    PendingSwap = 1,    // "Update not finished" — startup swap failed.
    Mismatch = 2        // "Some apps still run the old version" — swap done
                        // or DLL reported TSF_ABI_MISMATCH.
};

/// Read TSF-update flags from SharedState and decide banner visibility.
/// Single source of truth used by Sciter settings, Classic settings and tray.
[[nodiscard]] UpdateBannerState GetUpdateBannerState(const SharedStateManager& shared) noexcept;

}  // namespace NextKey
