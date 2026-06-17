// src/core/config/ConfigSnapshotBuilder.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// ConfigSnapshotBuilder::BuildFromToml — pure TOML→ConfigSnapshot transform.
// Lifted from HookEngine::RebuildSnapshotFromToml.
//
// Phase 1 of docs/plans/2026-05-22-hookengine-degod-probe.md.
//
// No HookEngine state is touched here. The isExcludedApp_ side-effect
// (runtime flag, not snapshot data) stays at the caller (HookEngine).
#include "core/config/ConfigSnapshotBuilder.h"

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <utility>

#include "core/config/ConfigManager.h"
#include "core/config/ConfigSnapshot.h"
#include "core/config/TypingConfig.h"

namespace NextKey::ConfigSnapshotBuilder {

std::shared_ptr<const ConfigSnapshot> BuildFromToml(
    const std::filesystem::path& configPath,
    bool excludeAppsEnabled,
    bool tsfAppsEnabled,
    bool macroEnabled,
    std::uint32_t generation) {

    // ConfigManager methods take std::wstring — extract from path.
    const std::wstring wPath = configPath.wstring();

    // Parse per-app overrides in one TOML pass; partition into the three
    // typed maps the snapshot expects (encoding, input method, send method).
    // All three publish through the same shared_ptr swap so
    // ClassifyFocusedWindow on main sees a consistent view even mid-rebuild.
    auto overrides = ConfigManager::LoadAppOverrides(wPath);
    std::unordered_map<std::wstring, CodeTable>    encOv;
    std::unordered_map<std::wstring, InputMethod>  imOv;
    std::unordered_map<std::wstring, std::int8_t>  sendOv;
    for (auto& [exe, entry] : overrides) {
        if (entry.encodingOverride >= 0)
            encOv.emplace(exe, static_cast<CodeTable>(entry.encodingOverride));
        if (entry.inputMethod >= 0)
            imOv.emplace(exe, static_cast<InputMethod>(entry.inputMethod));
        if (entry.sendMethod >= 0)
            sendOv.emplace(exe, entry.sendMethod);
    }

    std::unordered_set<std::wstring> excluded;
    std::unordered_set<std::wstring> forcedVn;
    if (excludeAppsEnabled) {
        for (auto& app : ConfigManager::LoadAllExcludedApps(wPath))
            excluded.insert(std::move(app));
        // Per-app hard-V list shares the excludeApps feature gate. Keep the two
        // sets DISJOINT: an exe in both lists resolves to excluded (transparent)
        // wins — see core/PerAppModeDecision.h. Dropping it here means the
        // runtime never observes an app in both, so a hand-edited config can't
        // double-lock.
        for (auto& app : ConfigManager::LoadForcedVnApps(wPath)) {
            if (!excluded.count(app)) forcedVn.insert(std::move(app));
        }
    }

    std::unordered_set<std::wstring> tsf;
    if (tsfAppsEnabled) {
        for (auto& app : ConfigManager::LoadTsfApps(wPath))
            tsf.insert(std::move(app));
    }

    std::unordered_map<std::wstring, std::wstring> macros;
    if (macroEnabled) {
        macros = ConfigManager::LoadMacros(wPath);
    }

    return std::make_shared<const ConfigSnapshot>(ConfigSnapshot::Build(
        std::move(macros),
        std::move(excluded),
        std::move(forcedVn),
        std::move(tsf),
        std::move(encOv),
        std::move(imOv),
        std::move(sendOv),
        generation));
}

}  // namespace NextKey::ConfigSnapshotBuilder
