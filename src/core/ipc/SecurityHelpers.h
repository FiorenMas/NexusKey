// VKey - Security Helper Utilities
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once
#include <windows.h>
#include <sddl.h>

namespace NextKey {

// Returns a SECURITY_ATTRIBUTES that grants full access only to SYSTEM and the
// creator/owner. Call LocalFree(sa.lpSecurityDescriptor) after the handle is created.
//
// SDDL breakdown:
//   D:PAI          — DACL, protected, auto-inherited
//   (A;;GA;;;SY)   — Allow GENERIC_ALL to SYSTEM
//   (A;;GA;;;CO)   — Allow GENERIC_ALL to Creator/Owner
inline SECURITY_ATTRIBUTES MakeCreatorOnlySecurityAttributes() noexcept {
    PSECURITY_DESCRIPTOR pSD = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:PAI(A;;GA;;;SY)(A;;GA;;;CO)",
        SDDL_REVISION_1,
        &pSD,
        nullptr);
    // If ConvertString fails (shouldn't on any supported Windows version),
    // pSD stays nullptr → CreateFileMapping uses default DACL → safer than crashing.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;
    return sa;
}

} // namespace NextKey
