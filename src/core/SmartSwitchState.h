// VKey - Smart Switch Shared Memory State
// SPDX-License-Identifier: AGPL-3.0-only
//
// Fixed-size struct for shared memory IPC.
// Stores per-app Vietnamese/English mode using hash-based lookup.

#pragma once

#include <cstdint>
#include <cwctype>

namespace NextKey {

/// Maximum number of apps tracked in shared memory
static constexpr uint32_t SMART_SWITCH_MAX_ENTRIES = 64;

/// Single entry in the smart switch table
struct SmartSwitchEntry {
    uint32_t hash;        // FNV-1a hash of lowercase exe name
    uint8_t  vietnamese;  // 0=English, 1=Vietnamese
    uint8_t  padding[3];  // Alignment
};

/// Shared memory layout for smart switch data
/// Named shared memory: "Local\\VKeySmartSwitch"
struct SmartSwitchState {
    uint32_t magic;    // 'SMSW' = 0x57534D53
    uint32_t epoch;    // Change counter
    uint32_t count;    // Number of valid entries
    uint32_t reserved;
    SmartSwitchEntry entries[SMART_SWITCH_MAX_ENTRIES];

    static constexpr uint32_t MAGIC_VALUE = 0x57534D53;  // 'SMSW'

    [[nodiscard]] bool IsValid() const noexcept {
        return magic == MAGIC_VALUE;
    }

    void InitDefaults() noexcept {
        magic = MAGIC_VALUE;
        epoch = 0;
        count = 0;
        reserved = 0;
        for (auto& e : entries) {
            e.hash = 0;
            e.vietnamese = 0;
            e.padding[0] = e.padding[1] = e.padding[2] = 0;
        }
    }
};

/// FNV-1a hash for exe names (case-insensitive: lowercases each char before hashing)
// Note: FNV-1a hash only, no name verification. Collision risk negligible for <=64 entries.
inline uint32_t FnvHash(const wchar_t* str) noexcept {
    uint32_t hash = 2166136261u;  // FNV offset basis
    while (*str) {
        hash ^= static_cast<uint32_t>(towlower(*str));
        hash *= 16777619u;  // FNV prime
        ++str;
    }
    return hash;
}

}  // namespace NextKey
