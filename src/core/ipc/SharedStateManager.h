// VKey - SharedStateManager
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "SharedState.h"
#include <memory>

namespace NextKey {

/// Manages shared memory IPC between Core and Engine
/// Creates/opens "Local\\VKeySharedState" shared memory region
class SharedStateManager {
public:
    SharedStateManager();
    ~SharedStateManager();

    // Non-copyable, movable
    SharedStateManager(const SharedStateManager&) = delete;
    SharedStateManager& operator=(const SharedStateManager&) = delete;
    SharedStateManager(SharedStateManager&&) noexcept;
    SharedStateManager& operator=(SharedStateManager&&) noexcept;

    /// Create shared memory (Core side)
    [[nodiscard]] bool Create();

    /// Open existing shared memory (Engine side, read-only)
    [[nodiscard]] bool Open();

    /// Open existing shared memory with read-write access (for flag toggling from DLL)
    [[nodiscard]] bool OpenReadWrite();

    /// Read current state (validates magic before returning)
    [[nodiscard]] SharedState Read() const noexcept;

    /// Write state (Core side only)
    void Write(const SharedState& state) noexcept;

    /// Read epoch directly from memory-mapped region (single 32-bit read, for hot path)
    /// Used to skip full Read() when SharedState hasn't changed.
    [[nodiscard]] uint32_t ReadEpoch() const noexcept;

    /// Read flags directly from memory-mapped region (zero-copy, for hot path)
    [[nodiscard]] uint32_t ReadFlags() const noexcept;

    /// Toggle a flag bit atomically (safe for concurrent access from DLL/EXE)
    /// Requires OpenReadWrite() or Create()
    void ToggleFlag(uint32_t flagBit) noexcept;

    /// Set or clear a flag bit atomically (safe for concurrent access)
    /// Requires OpenReadWrite() or Create()
    void SetOrClearFlag(uint32_t flagBit, bool set) noexcept;

    /// Read the HookContextAnchor (TSF readonly → Hook) with seqlock validation.
    /// Returns true on consistent snapshot; false on 3 retries exhausted.
    /// Callers should treat failure as "anchor unavailable" (fallback path).
    [[nodiscard]] bool ReadAnchor(HookContextAnchor& out) const noexcept;

    /// Write the HookContextAnchor from TSF readonly DLL.
    /// Requires OpenReadWrite() or Create(). Bumps generation internally.
    void WriteAnchor(const HookContextAnchor& in) noexcept;

    /// Check if connected to valid shared memory
    [[nodiscard]] bool IsConnected() const noexcept;

    /// Check ABI compatibility without going through the seqlock-protected
    /// Read() path. magic / structVersion / structSize are written once at
    /// Create() and never mutated, so a direct aligned 32-bit read is correct
    /// and — unlike Read() — cannot spuriously report mismatch under contention.
    /// Used by the TSF DLL to decide whether to passthrough.
    [[nodiscard]] bool IsAbiCompatible() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

}  // namespace NextKey
