// VKey - Debug Log enable-confirmation popup
// SPDX-License-Identifier: AGPL-3.0-only
//
// Shown when the user clicks the "Bật debug log" toggle to ON in either UI
// (Modern Sciter, Classic Win32). The log captures raw key events and may
// contain passwords / sensitive data if left on; users should review and
// trim before sharing. Header-only (no .cpp) so both targets pick it up
// without a CMake edit.

#pragma once

#ifdef _WIN32
#include <Windows.h>

namespace NextKey {

/// Prompt the user with an OK/Cancel security warning before enabling debug log.
/// Returns true if the user clicked OK (proceed enabling); false on Cancel.
/// `englishUi` toggles VI/EN copy — pass `systemConfig_.language == 1`.
[[nodiscard]] inline bool ShowDebugLogWarning(HWND parent, bool englishUi) noexcept {
    const wchar_t* title;
    const wchar_t* body;
    if (englishUi) {
        title = L"Security warning — Debug Log";
        body =
            L"Enabling Debug Log records every keystroke you type, including "
            L"raw keys. The log file may capture passwords or other sensitive "
            L"data if left enabled for a long time.\n\n"
            L"Before sharing the log file with anyone:\n"
            L"  • Open the file and review its contents\n"
            L"  • Delete unrelated lines using the timestamps\n"
            L"  • Send only the portion needed for debugging\n\n"
            L"Proceed?";
    } else {
        title = L"Cảnh báo bảo mật — Debug Log";
        body =
            L"Bật Debug Log sẽ ghi lại toàn bộ thao tác bàn phím của bạn, "
            L"bao gồm cả raw key. File log có thể chứa mật khẩu hoặc thông "
            L"tin nhạy cảm nếu bật trong thời gian dài.\n\n"
            L"Trước khi gửi file log cho ai khác:\n"
            L"  • Mở file log và xem lại nội dung\n"
            L"  • Xóa các dòng không liên quan dựa theo thời gian\n"
            L"  • Chỉ gửi đoạn cần thiết cho việc debug\n\n"
            L"Tiếp tục bật Debug Log?";
    }
    int res = ::MessageBoxW(parent, body, title,
                            MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
    return res == IDOK;
}

}  // namespace NextKey

#endif  // _WIN32
