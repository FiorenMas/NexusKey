// NexusKey - Macro Phrase-Prefix Matcher
// SPDX-License-Identifier: GPL-3.0-only
//
// Pure, platform-free helper used by HookEngine to decide whether a
// space-terminated word boundary should preserve the macro buffer (because it
// looks like the user is mid-way through typing a multi-word macro key) or
// clear it (normal word-commit behavior).

#pragma once

#include <cwctype>
#include <string>
#include <unordered_set>

namespace NextKey {

/// Returns true if `candidate` is a non-empty prefix of at least one key in
/// `spaceKeys`. Case sensitivity follows the macro case rule:
///   - key has any uppercase letter → strict prefix (typed case must match)
///   - key is all-lowercase          → case-insensitive prefix (ASCII-fold)
///
/// Empty `spaceKeys` short-circuits to false — this is the hot path for users
/// who haven't defined any multi-word macros.
[[nodiscard]] inline bool IsSpaceMacroPrefix(
    const std::wstring& candidate,
    const std::unordered_set<std::wstring>& spaceKeys) noexcept {
    if (spaceKeys.empty() || candidate.empty()) return false;

    auto hasUpper = [](const std::wstring& s) noexcept {
        for (auto c : s) if (iswupper(c)) return true;
        return false;
    };

    std::wstring candLower;  // built lazily, only if a lowercase key is found
    for (const auto& key : spaceKeys) {
        if (key.size() < candidate.size()) continue;
        if (hasUpper(key)) {
            if (key.compare(0, candidate.size(), candidate) == 0) return true;
        } else {
            if (candLower.empty()) {
                candLower = candidate;
                for (auto& c : candLower) c = static_cast<wchar_t>(towlower(c));
            }
            if (key.compare(0, candLower.size(), candLower) == 0) return true;
        }
    }
    return false;
}

}  // namespace NextKey
