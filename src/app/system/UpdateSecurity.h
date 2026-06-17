// VKey - Update Security Helpers
// SPDX-License-Identifier: AGPL-3.0-only
//
// Security hardening for the self-update system:
// - SHA-256 hash verification of downloaded files (SEC-001)
// - PowerShell argument escaping (SEC-002)
// - Download URL domain validation (SEC-003)

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

namespace NextKey {

// ── SEC-002: PowerShell single-quote escaping ──────────────────────────────

/// Escape a string for safe use inside PowerShell single-quoted strings.
/// Replaces every `'` with `''` (PowerShell's single-quote escape sequence).
/// Example: `C:\Users\O'Brien\file` -> `C:\Users\O''Brien\file`
[[nodiscard]] std::wstring EscapePowerShellSingleQuote(const std::wstring& input) noexcept;

// ── SEC-003: URL domain validation ─────────────────────────────────────────

/// Validate that a download URL points to an allowed GitHub domain.
/// Allowed prefixes:
///   https://github.com/
///   https://objects.githubusercontent.com/
///   https://codeload.github.com/
/// Returns true if the URL starts with one of the allowed prefixes.
[[nodiscard]] bool IsAllowedDownloadUrl(const std::wstring& url) noexcept;

/// Overload for narrow strings (used by FindAssetUrl which returns std::string).
[[nodiscard]] bool IsAllowedDownloadUrl(const std::string& url) noexcept;

// ── SEC-001: SHA-256 hash verification ─────────────────────────────────────

/// Parse a .sha256 checksum file (format: "<hex>  <filename>" or just "<hex>").
/// Returns the extracted hex hash in lowercase, or empty string if unparseable.
[[nodiscard]] std::string ParseSha256File(const std::string& content) noexcept;

#ifdef _WIN32

/// Compute SHA-256 hash of a file using Windows CNG (bcrypt.h).
/// Returns lowercase hex string (64 chars), or empty string on failure.
/// Thread-safe — creates/destroys its own CNG handles.
[[nodiscard]] std::string ComputeFileSha256(const std::wstring& filePath) noexcept;

/// Download the .sha256 sidecar for a ZIP URL and verify the ZIP's integrity.
/// Precondition: zipUrl must have already been validated by IsAllowedDownloadUrl.
/// 1. Appends ".sha256" to zipUrl → downloads checksum file
/// 2. Parses expected hash from checksum content
/// 3. Computes actual SHA-256 of localZipPath
/// 4. Returns true if hashes match
///
/// On any failure (download, parse, hash mismatch) returns false.
[[nodiscard]] bool VerifyDownloadedZip(
    const std::wstring& zipUrl,
    const std::wstring& localZipPath,
    std::atomic<bool>& cancelFlag) noexcept;

#endif  // _WIN32

}  // namespace NextKey
