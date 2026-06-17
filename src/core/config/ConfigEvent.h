// VKey - Config Event Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <memory>

namespace NextKey {

/// Named Event for signaling config changes between Core and Engine
/// Core signals the event when settings change
/// Engine waits on the event to detect changes
class ConfigEvent {
public:
    ConfigEvent();
    ~ConfigEvent();

    // Non-copyable, movable
    ConfigEvent(const ConfigEvent&) = delete;
    ConfigEvent& operator=(const ConfigEvent&) = delete;
    ConfigEvent(ConfigEvent&&) noexcept;
    ConfigEvent& operator=(ConfigEvent&&) noexcept;

    /// Create or open the named event
    /// Returns true if successful
    bool Initialize();

    /// Signal the event (Core side - notify Engine of config change)
    void Signal();

    /// Wait for signal with timeout (Engine side)
    /// Returns true if signaled, false if timeout
    bool Wait(unsigned int timeoutMs = 0);

    /// Check if event is valid
    bool IsValid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

}  // namespace NextKey
