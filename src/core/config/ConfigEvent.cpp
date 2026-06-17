// VKey - Config Event Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "ConfigEvent.h"
#include <Windows.h>
#include "core/Debug.h"

namespace NextKey {

// Named event for config change notifications
static constexpr wchar_t kEventName[] = L"Local\\VKeyConfigEvent";

struct ConfigEvent::Impl {
    HANDLE hEvent = nullptr;
};

ConfigEvent::ConfigEvent() : pImpl_(std::make_unique<Impl>()) {}

ConfigEvent::~ConfigEvent() {
    if (pImpl_ && pImpl_->hEvent) {
        CloseHandle(pImpl_->hEvent);
        pImpl_->hEvent = nullptr;
    }
}

ConfigEvent::ConfigEvent(ConfigEvent&& other) noexcept = default;
ConfigEvent& ConfigEvent::operator=(ConfigEvent&& other) noexcept = default;

bool ConfigEvent::Initialize() {
    if (pImpl_->hEvent) return true;  // Already initialized

    // Create or open named event
    // Auto-reset event: resets after single wait is satisfied
    // Note: default security is sufficient — this event carries no sensitive data,
    // it is purely a notification signal scoped to Local\ namespace.
    pImpl_->hEvent = CreateEventW(
        nullptr,        // default security
        FALSE,          // auto-reset event
        FALSE,          // initial state: not signaled
        kEventName      // named event for cross-process
    );

    if (!pImpl_->hEvent) {
        NEXTKEY_LOG(L"ConfigEvent: Failed to create event");
        return false;
    }

    // Check if we created or opened existing
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        NEXTKEY_LOG(L"ConfigEvent: Opened existing event");
    } else {
        NEXTKEY_LOG(L"ConfigEvent: Created new event");
    }

    return true;
}

void ConfigEvent::Signal() {
    if (pImpl_->hEvent) {
        SetEvent(pImpl_->hEvent);
        NEXTKEY_LOG(L"ConfigEvent: Signaled");
    }
}

bool ConfigEvent::Wait(unsigned int timeoutMs) {
    if (!pImpl_->hEvent) return false;

    DWORD result = WaitForSingleObject(pImpl_->hEvent, timeoutMs);
    return (result == WAIT_OBJECT_0);
}

bool ConfigEvent::IsValid() const {
    return pImpl_ && pImpl_->hEvent != nullptr;
}

}  // namespace NextKey
