// VKey - Smart Switch Shared Memory Manager
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "SmartSwitchState.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace NextKey {

/// Manages shared memory for per-app Vietnamese/English mode tracking.
/// Uses "Local\\VKeySmartSwitch" named shared memory.
class SmartSwitchManager {
public:
    SmartSwitchManager();
    ~SmartSwitchManager();

    SmartSwitchManager(const SmartSwitchManager&) = delete;
    SmartSwitchManager& operator=(const SmartSwitchManager&) = delete;

    /// Create shared memory (EXE side — owner)
    [[nodiscard]] bool Create();

    /// Open existing shared memory with read-write access
    [[nodiscard]] bool OpenReadWrite();

    /// Set app mode (inserts or updates entry)
    void SetAppMode(const std::wstring& exeName, bool vietnamese) noexcept;

    /// Get app mode (returns nullopt if app not found)
    [[nodiscard]] std::optional<bool> GetAppMode(const std::wstring& exeName) const noexcept;

    /// Populate shared memory from a map (called at startup from TOML data)
    void LoadFromMap(const std::unordered_map<std::wstring, bool>& map) noexcept;

    /// Check if connected
    [[nodiscard]] bool IsConnected() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

}  // namespace NextKey
