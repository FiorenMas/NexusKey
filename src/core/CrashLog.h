// NexusKey - Always-on crash exception logger
// SPDX-License-Identifier: GPL-3.0-only
//
// Appends a single-line record to `<exe_dir>\_nexuskey_crash.log` when an
// exception reaches a top-level callback / thread entry. Unlike NEXTKEY_LOG
// (debug-only), CrashLog is compiled in for Release builds — it's the only
// breadcrumb left when the catch handler swallows a throw.

#pragma once

namespace NextKey {

/// Append `[timestamp] context: what` to the crash log. Safe to call from
/// any thread; never throws.
void CrashLog(const wchar_t* context, const char* what) noexcept;

}  // namespace NextKey
