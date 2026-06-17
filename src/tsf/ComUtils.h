// VKey - COM Utility Functions
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <Windows.h>
#include <unknwn.h>

namespace NextKey {
namespace TSF {

// Safe COM pointer release helper
template<typename T>
inline void SafeRelease(T*& pInterface) {
    if (pInterface) {
        pInterface->Release();
        pInterface = nullptr;
    }
}

// DLL reference count helpers
inline void DllAddRef() {
    extern LONG g_dllRefCount;
    InterlockedIncrement(&g_dllRefCount);
}

inline void DllRelease() {
    extern LONG g_dllRefCount;
    InterlockedDecrement(&g_dllRefCount);
}

}  // namespace TSF
}  // namespace NextKey
