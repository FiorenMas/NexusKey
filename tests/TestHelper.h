// VKey - Shared Test Utilities
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>

namespace NextKey::Testing {

/// Type a string character-by-character into any engine that has PushChar().
template<typename Engine>
void TypeString(Engine& engine, const wchar_t* input) {
    for (const wchar_t* p = input; *p; ++p) {
        engine.PushChar(*p);
    }
}

template<typename Engine>
void TypeString(Engine& engine, const std::wstring& input) {
    for (wchar_t c : input) {
        engine.PushChar(c);
    }
}

}  // namespace NextKey::Testing
