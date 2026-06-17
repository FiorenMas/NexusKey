// VKey - Win32 locale-aware case mapper for Macro::CaseMapper
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
//
// Production CaseMapper that wraps CharUpperBuffW / CharLowerBuffW.
// These are locale-aware so Vietnamese diacritics ('ô' ↔ 'Ô') flip
// correctly, unlike towupper/towlower which only handle ASCII under
// the C locale.

#pragma once

#include <windows.h>

#include "core/MacroCase.h"

namespace NextKey {

class Win32CaseMapper final : public Macro::CaseMapper {
public:
    void Upper(wchar_t* buf, std::size_t n) const override {
        ::CharUpperBuffW(buf, static_cast<DWORD>(n));
    }
    void Lower(wchar_t* buf, std::size_t n) const override {
        ::CharLowerBuffW(buf, static_cast<DWORD>(n));
    }
};

}  // namespace NextKey
