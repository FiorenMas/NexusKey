// src/core/config/ConfigSnapshotBuilder.h
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pure free function: TOML config file → immutable ConfigSnapshot.
// Lifted from HookEngine::RebuildSnapshotFromToml (HookEngine.cpp).
//
// Part of the ConfigApplier probe — Phase 1 of
// docs/plans/2026-05-22-hookengine-degod-probe.md.
//
// Windows-only at link time (depends on ConfigManager which requires Win32
// for string conversions). Header is platform-portable; implementation is
// guarded by WIN32 in CMakeLists.txt.
#pragma once

#include <filesystem>
#include <memory>
#include <cstdint>

namespace NextKey {

struct ConfigSnapshot;

namespace ConfigSnapshotBuilder {

// Parse the live TOML config at `configPath` and build an immutable
// ConfigSnapshot. Pure — no global / engine state mutation. Caller decides
// what to do with side-effects (e.g. clearing isExcludedApp_ when excludeApps
// is off — see HookEngine::RebuildSnapshotFromToml caller).
//
// Reads:
//   - `[app_overrides]` (encoding / input-method / send-method per exe)
//   - `[excluded_apps]` (only if `excludeAppsEnabled` is true)
//   - `[tsf_apps]`      (only if `tsfAppsEnabled` is true)
//   - `[macros]`        (only if `macroEnabled` is true)
//
// Returns a heap-allocated, ready-to-publish snapshot.
[[nodiscard]] std::shared_ptr<const ConfigSnapshot> BuildFromToml(
    const std::filesystem::path& configPath,
    bool excludeAppsEnabled,
    bool tsfAppsEnabled,
    bool macroEnabled,
    std::uint32_t generation);

}  // namespace ConfigSnapshotBuilder
}  // namespace NextKey
