// NexusKey - SharedStateManager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "SharedStateManager.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <atomic>

namespace NextKey {

static constexpr const wchar_t* SHARED_MEM_NAME = L"Local\\NexusKeySharedState";
static constexpr int SEQLOCK_MAX_RETRIES = 3;

struct SharedStateManager::Impl {
#ifdef _WIN32
    HANDLE hMapping = nullptr;
    volatile SharedState* pState = nullptr;  // volatile: mapped across processes
#endif
    bool isOwner = false;
    bool isWritable = false;  // true for Create() and OpenReadWrite()
};

SharedStateManager::SharedStateManager() : pImpl_(std::make_unique<Impl>()) {}

SharedStateManager::~SharedStateManager() {
#ifdef _WIN32
    if (pImpl_->pState) {
        UnmapViewOfFile(const_cast<SharedState*>(pImpl_->pState));
    }
    if (pImpl_->hMapping) {
        CloseHandle(pImpl_->hMapping);
    }
#endif
}

SharedStateManager::SharedStateManager(SharedStateManager&&) noexcept = default;
SharedStateManager& SharedStateManager::operator=(SharedStateManager&&) noexcept = default;

bool SharedStateManager::Create() {
#ifdef _WIN32
    // Default DACL (nullptr): grants access to creator's user SID + SYSTEM + Admins.
    // "Local\" namespace scopes to current session — no cross-session exposure.
    // Note: MakeCreatorOnlySecurityAttributes() DACL uses CO (Creator Owner) SID which
    // is NOT resolved for non-container objects → blocks same-user subprocesses.
    pImpl_->hMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SharedState),
        SHARED_MEM_NAME
    );

    if (!pImpl_->hMapping) {
        return false;
    }

    pImpl_->pState = static_cast<volatile SharedState*>(
        MapViewOfFile(pImpl_->hMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState))
    );

    if (!pImpl_->pState) {
        CloseHandle(pImpl_->hMapping);
        pImpl_->hMapping = nullptr;
        return false;
    }

    // Initialize with defaults (epoch starts at 0 = even = stable)
    auto* p = const_cast<SharedState*>(pImpl_->pState);
    p->InitDefaults();
    pImpl_->isOwner = true;
    pImpl_->isWritable = true;

    return true;
#else
    return false;
#endif
}

bool SharedStateManager::Open() {
#ifdef _WIN32
    pImpl_->hMapping = OpenFileMappingW(
        FILE_MAP_READ,
        FALSE,
        SHARED_MEM_NAME
    );

    if (!pImpl_->hMapping) {
        return false;
    }

    pImpl_->pState = static_cast<volatile SharedState*>(
        MapViewOfFile(pImpl_->hMapping, FILE_MAP_READ, 0, 0, sizeof(SharedState))
    );

    if (!pImpl_->pState) {
        CloseHandle(pImpl_->hMapping);
        pImpl_->hMapping = nullptr;
        return false;
    }

    return pImpl_->pState->magic == SharedState::MAGIC_VALUE;
#else
    return false;
#endif
}

bool SharedStateManager::OpenReadWrite() {
#ifdef _WIN32
    // Clean up any existing mapping to prevent handle leaks on re-open
    if (pImpl_->pState) {
        UnmapViewOfFile(const_cast<SharedState*>(pImpl_->pState));
        pImpl_->pState = nullptr;
    }
    if (pImpl_->hMapping) {
        CloseHandle(pImpl_->hMapping);
        pImpl_->hMapping = nullptr;
    }

    pImpl_->hMapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        SHARED_MEM_NAME
    );

    if (!pImpl_->hMapping) {
        return false;
    }

    pImpl_->pState = static_cast<volatile SharedState*>(
        MapViewOfFile(pImpl_->hMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState))
    );

    if (!pImpl_->pState) {
        CloseHandle(pImpl_->hMapping);
        pImpl_->hMapping = nullptr;
        return false;
    }

    pImpl_->isWritable = true;
    return pImpl_->pState->magic == SharedState::MAGIC_VALUE;
#else
    return false;
#endif
}

SharedState SharedStateManager::Read() const noexcept {
    SharedState state{};
#ifdef _WIN32
    if (!pImpl_->pState || pImpl_->pState->magic != SharedState::MAGIC_VALUE) {
        return state;
    }

    // Seqlock read: retry if epoch is odd (write in progress) or changed during copy
    for (int i = 0; i < SEQLOCK_MAX_RETRIES; ++i) {
        // Acquire fence before reading epoch: prevents CPU/compiler from reordering
        // the epoch read after the struct copy (correct on x86 TSO and ARM).
        uint32_t before = static_cast<volatile const SharedState*>(pImpl_->pState)->epoch;
        std::atomic_thread_fence(std::memory_order_acquire);

        // Copy the entire struct
        state = *const_cast<const SharedState*>(pImpl_->pState);

        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t after = static_cast<volatile const SharedState*>(pImpl_->pState)->epoch;

        // Stable if epoch didn't change and is even (not mid-write)
        if (before == after && (before & 1) == 0) {
            return state;
        }
        // Yield and retry
        YieldProcessor();
    }

    // Seqlock failed: return default state (magic=0 so IsValid() returns false)
    state = {};
#endif
    return state;
}

void SharedStateManager::Write(const SharedState& state) noexcept {
#ifdef _WIN32
    if (!pImpl_->pState || !pImpl_->isWritable) {
        return;
    }

    auto* p = const_cast<SharedState*>(pImpl_->pState);

    // Seqlock write protocol:
    //   epoch 0(even) → 1(odd=writing) → copy data → 2(even=done)
    // Callers should NOT modify epoch — Write() auto-increments it.
    uint32_t seq = p->epoch + 1;  // Now odd = writing
    p->epoch = seq;
    MemoryBarrier();

    // Copy all data fields (epoch is managed by seqlock, not caller).
    // ⚠️  When adding new SharedState fields, MUST add copy here too!
    // Read() uses full struct copy (*p = state), but Write() copies field-by-field
    // to skip epoch. Missing a field here silently drops it — works in Debug
    // (which reads TOML directly) but fails in Release (SharedState path only).
    //
    // NOTE: contextAnchor is intentionally NOT copied here — TSF DLL writes it
    // via its own seqlock (WriteAnchorSeqlock). Copying from `state` would race
    // with the TSF writer and clobber live data. Use WriteAnchor() instead.
    p->magic = state.magic;
    p->structVersion = state.structVersion;
    p->structSize = state.structSize;
    p->flags = state.flags;
    p->inputMethod = state.inputMethod;
    p->spellCheck = state.spellCheck;
    p->optimizeLevel = state.optimizeLevel;
    p->featureFlags[0] = state.featureFlags[0];
    p->featureFlags[1] = state.featureFlags[1];
    p->extFeatureFlags = state.extFeatureFlags;
    p->codeTable = state.codeTable;
    p->hotkeyMods = state.hotkeyMods;
    p->hotkeyKeyLo = state.hotkeyKeyLo;
    p->hotkeyKeyHi = state.hotkeyKeyHi;
    p->convertMods = state.convertMods;
    p->convertKeyLo = state.convertKeyLo;
    p->convertKeyHi = state.convertKeyHi;
    p->configGeneration = state.configGeneration;
    p->reserved0 = state.reserved0;
    memcpy(p->reserved, state.reserved, sizeof(state.reserved));

    MemoryBarrier();
    p->epoch = seq + 1;  // Now even = done
#endif
}

uint32_t SharedStateManager::ReadEpoch() const noexcept {
#ifdef _WIN32
    if (pImpl_->pState && pImpl_->pState->magic == SharedState::MAGIC_VALUE) {
        // Single 32-bit read is atomic on x86 — no seqlock needed
        return pImpl_->pState->epoch;
    }
#endif
    return 0;
}

uint32_t SharedStateManager::ReadFlags() const noexcept {
#ifdef _WIN32
    if (pImpl_->pState && pImpl_->pState->magic == SharedState::MAGIC_VALUE) {
        // Single 32-bit read is atomic on x86 — no seqlock needed
        return pImpl_->pState->flags;
    }
#endif
    return 0;
}

void SharedStateManager::ToggleFlag(uint32_t flagBit) noexcept {
#ifdef _WIN32
    if (!pImpl_->pState || pImpl_->pState->magic != SharedState::MAGIC_VALUE) return;

    // Atomic 32-bit XOR — safe for concurrent DLL/EXE access
    auto* flagsAddr = &(const_cast<SharedState*>(pImpl_->pState)->flags);
    InterlockedXor(reinterpret_cast<volatile LONG*>(flagsAddr), static_cast<LONG>(flagBit));
#endif
}

void SharedStateManager::SetOrClearFlag(uint32_t flagBit, bool set) noexcept {
#ifdef _WIN32
    if (!pImpl_->pState || pImpl_->pState->magic != SharedState::MAGIC_VALUE) return;

    auto* flagsAddr = reinterpret_cast<volatile LONG*>(
        &(const_cast<SharedState*>(pImpl_->pState)->flags));
    if (set) {
        InterlockedOr(flagsAddr, static_cast<LONG>(flagBit));
    } else {
        InterlockedAnd(flagsAddr, ~static_cast<LONG>(flagBit));
    }
#endif
}

bool SharedStateManager::ReadAnchor(HookContextAnchor& out) const noexcept {
#ifdef _WIN32
    if (!pImpl_->pState || pImpl_->pState->magic != SharedState::MAGIC_VALUE) {
        return false;
    }
    // Take address of the anchor field in the memory-mapped region.
    // const_cast peels the volatile-on-pointer; ReadAnchorSeqlock takes volatile*.
    const volatile HookContextAnchor* anchor =
        &const_cast<const SharedState*>(pImpl_->pState)->contextAnchor;
    return ReadAnchorSeqlock(anchor, out);
#else
    (void)out;
    return false;
#endif
}

void SharedStateManager::WriteAnchor(const HookContextAnchor& in) noexcept {
#ifdef _WIN32
    if (!pImpl_->pState || !pImpl_->isWritable) return;
    if (pImpl_->pState->magic != SharedState::MAGIC_VALUE) return;
    volatile HookContextAnchor* anchor =
        &const_cast<SharedState*>(pImpl_->pState)->contextAnchor;
    WriteAnchorSeqlock(anchor, in);
#else
    (void)in;
#endif
}

bool SharedStateManager::IsConnected() const noexcept {
#ifdef _WIN32
    return pImpl_->pState != nullptr && pImpl_->pState->magic == SharedState::MAGIC_VALUE;
#else
    return false;
#endif
}

bool SharedStateManager::IsAbiCompatible() const noexcept {
#ifdef _WIN32
    if (!pImpl_->pState) return false;
    // Direct aligned 32-bit reads — these three fields are set once by
    // Create()'s InitDefaults() before the mapping becomes observable to any
    // other process and never change afterwards, so no seqlock is required.
    const uint32_t magic = pImpl_->pState->magic;
    const uint32_t ver   = pImpl_->pState->structVersion;
    const uint32_t size  = pImpl_->pState->structSize;
    return magic == SharedState::MAGIC_VALUE
        && ver <= SharedState::CURRENT_VERSION
        && size >= 24;
#else
    return false;
#endif
}

}  // namespace NextKey
