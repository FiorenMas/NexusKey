// VKey - Windows UTF-8/Wide String Conversion Utilities
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <string>

namespace NextKey {

/// Convert a wide string (UTF-16) to a UTF-8 std::string.
[[nodiscard]] inline std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(), len, nullptr, nullptr);
    return result;
}

/// Convert a UTF-8 std::string to a wide string (UTF-16).
[[nodiscard]] inline std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

}  // namespace NextKey

#endif  // _WIN32
