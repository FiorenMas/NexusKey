// VKey - TSF Registration & Diagnostics
// SPDX-License-Identifier: AGPL-3.0-only
//
// Functions for registering/unregistering the TSF input method DLL,
// and diagnostic output. Used by EXE to manage DLL registration
// when user enables/disables TSF apps feature.

#pragma once

#include <string>

namespace NextKey {

/// Get path to VKeyTSF.dll (same directory as exe)
std::wstring GetTsfDllPath();

/// Check if TSF is registered by looking for CLSID in registry
bool IsTsfRegistered();

/// Register TSF DLL (requires admin for HKEY_CLASSES_ROOT)
bool RegisterTsf();

/// Unregister TSF DLL
bool UnregisterTsf();

/// Run registration with admin elevation via ShellExecute
bool RegisterTsfElevated();

/// Run unregistration with admin elevation
bool UnregisterTsfElevated();

/// Diagnostic output — enumerates HKLs, TSF profiles, active profile, SharedState
void RunDiagnostics();

/// Remove any HKCU CLSID override for the VKey TSF DLL.
/// Malware can write HKCU\Software\Classes\CLSID\{guid}\InprocServer32 to redirect
/// DLL loading in all TSF-aware apps. Call this at startup to clean it up.
void CleanupHkcuClsidOverride() noexcept;

/// Activate the VKey TSF profile programmatically
bool ActivateVKeyTsfProfile();

}  // namespace NextKey
