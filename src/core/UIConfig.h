// VKey - UI Configuration
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

/// UI configuration for app appearance and behavior
/// Stored in [ui] section of config.toml
struct UIConfig {
    // Panel state
    bool showAdvanced = false;      // Advanced settings panel expanded

    // Appearance
    uint8_t backgroundOpacity = 80; // 0-100, background transparency

    // Window state (future)
    bool pinned = false;            // Window always on top

    // Default constructor
    UIConfig() = default;
};

}  // namespace NextKey
