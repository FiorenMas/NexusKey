// VKey - Runtime-gated debug logger
// SPDX-License-Identifier: AGPL-3.0-only
//
// Single backend for NEXTKEY_LOG / HOOK_LOG / TSF_LOG. Compiled unconditionally
// in every build; runtime-gated by an atomic. When disabled, Log() returns
// after an acquire atomic load — fast enough for cold paths and acceptable
// for the hot path because most call sites are off the keystroke critical
// line. In _DEBUG / NEXTKEY_DEBUG builds the same formatted line is fanned
// out to OutputDebugStringW so DebugView traces keep working.
//
// File path resolution (Win32):
//   1. <install dir>\VKey_<RoleTag>_<DDMMYYYY>_<HHMM>[_p<PID>].log if writable
//   2. %APPDATA%\VKey\logs\... otherwise
//
// RoleTag is set once at process init via SetRoleTag(): "Modern" (Sciter EXE),
// "Classic" (Win32 EXE), "TSF-<host>" (DLL hosts — chrome, Electron…),
// "Watchdog". Users tracking bug reports recognize the file at a glance.
// If SetRoleTag is not called (tests, legacy callers) the filename falls back
// to VKey_<ProcessTag>_<PID>.log.
//
// PID suffix is appended only for TSF-* roles (multiple DLL hosts per box) or
// as a same-minute-collision fallback (toggle off→on within one minute). Main
// EXE gets the clean form VKey_Modern_21052026_2013.log.
//
// Timestamp is captured at file-creation moment (first Log() after
// SetEnabled(true)) so multiple enable/disable cycles produce distinct files.
// Append-mode + per-line fflush still applies inside one open session.
//
// Linux: only used by tests via SetLogPathForTesting().

#pragma once

#include <atomic>
#include <cstdarg>
#include <string>

namespace NextKey {

class Logger {
public:
    static void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] static bool IsEnabled() noexcept {
        return enabled_.load(std::memory_order_acquire);
    }

    /// EXE/DLL init reports the install directory (parent of VKeyApp.exe /
    /// VKeyTSF.dll). Logger probes writability, falls back to APPDATA.
    /// If logging is currently enabled, any open file is closed and reopened
    /// at the new path on the next Log() — calling this from DllMain after
    /// EngineController already enabled the logger still routes future writes
    /// to the install dir.
    static void SetInstallDir(const std::wstring& dir) noexcept;

    /// Process role discriminator embedded in the log filename. Called once
    /// during process init: "Modern" / "Classic" for the EXE, "TSF-<host>"
    /// for the DLL, "Watchdog" for the supervisor. Empty string restores the
    /// legacy VKey_<ProcessTag>_<PID>.log filename (used by tests).
    /// Closes any currently open file so the next Log() reopens at the new
    /// path with a fresh capture-time timestamp.
    static void SetRoleTag(const std::wstring& tag) noexcept;

    /// Resolved file path for the current process. Computed lazily on first
    /// Log() while enabled; empty until then.
    [[nodiscard]] static std::wstring GetCurrentLogPath() noexcept;

    /// Folder containing the current process's log file (for the "open folder"
    /// button in the System tab). Resolves the same way as GetCurrentLogPath
    /// but without requiring the file to exist yet.
    [[nodiscard]] static std::wstring GetCurrentLogFolder() noexcept;

    static void Log(const wchar_t* fmt, ...) noexcept;

    /// Tests inject an exact path here. Empty string clears.
    static void SetLogPathForTesting(const std::wstring& path) noexcept;

    /// Flush + close. Idempotent. Called from app shutdown.
    static void Shutdown() noexcept;

private:
    static void LogV(const wchar_t* fmt, va_list args) noexcept;
    static std::atomic<bool> enabled_;
};

}  // namespace NextKey
