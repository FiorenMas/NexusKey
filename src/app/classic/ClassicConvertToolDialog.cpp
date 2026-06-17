// VKey Classic — Convert Tool Dialog Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "ClassicConvertToolDialog.h"
#include "ClassicHotkeyCapture.h"
#include "core/config/ConfigManager.h"
#include "core/CrashLog.h"
#include "app/helpers/AppHelpers.h"
#include "core/engine/CodeTableConverter.h"
#include "core/hotkey/HotkeyLabel.h"
#include "core/Strings.h"

#include <windowsx.h>
#include <commdlg.h>
#include <exception>

namespace NextKey::Classic {

enum {
    IDC_CHECK_ALL_CAPS = 3301,
    IDC_CHECK_ALL_LOWER,
    IDC_CHECK_CAPS_FIRST,
    IDC_CHECK_CAPS_EACH,
    IDC_CHECK_REMOVE_MARK,
    IDC_CHECK_ALERT_DONE,
    IDC_CHECK_AUTO_PASTE,
    IDC_CHECK_SEQUENTIAL,
    IDC_CHECK_ENABLE_LOG,
    IDC_COMBO_SOURCE,
    IDC_COMBO_DEST,
    IDC_BTN_RECORD_HOTKEY,
    IDC_BTN_CLEAR_HOTKEY,
    IDC_BTN_CONVERT,
    IDC_BTN_CLOSE_DLG,
    IDC_RADIO_CLIPBOARD,
    IDC_RADIO_FILE,
    IDC_EDIT_SOURCE_PATH,
    IDC_BTN_BROWSE_SOURCE,
    IDC_EDIT_DEST_PATH,
    IDC_BTN_BROWSE_DEST,
};

// ════════════════════════════════════════════════════════════

bool ClassicConvertToolDialog::Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
    ClassicConvertToolDialog dlg;
    if (!dlg.Init(hInstance, parent, forceLightTheme)) return false;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg.hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return dlg.modified_;
}

bool ClassicConvertToolDialog::Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
    hInstance_ = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbWndExtra = sizeof(void*);
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Công cụ chuyển đổi",
        style, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    dpi_ = Classic::GetWindowDpi(hwnd_);

    int w = Dpi(kWidth), h = Dpi(kHeight);
    RECT rc = {0, 0, w, h};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_TOPMOST);
    int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
    POINT pt = NextKey::GetCenteredPos(hwnd_, aw, ah);
    SetWindowPos(hwnd_, nullptr, pt.x, pt.y, aw, ah, SWP_NOZORDER);

    theme_.Init(hwnd_, forceLightTheme);
    theme_.ApplyWindowAttributes(hwnd_);

    config_ = ConfigManager::LoadConvertConfigOrDefault();
    CreateControls();
    PopulateFromConfig();
    UpdateFileMode();  // Hide file controls initially (clipboard mode)

    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicConvertToolDialog*>(lp);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(self->theme_.Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    theme_.ThemeAllChildren(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

// ════════════════════════════════════════════════════════════

void ClassicConvertToolDialog::CreateControls() {
    int x = Dpi(kPadding), y = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int rowH = Dpi(kRowH);
    int gap = Dpi(kRowGap);
    int btnH = Dpi(kBtnHeight);
    int colW = (cw - Dpi(8)) / 2;
    int col2X = x + colW + Dpi(8);

    auto check = [&](const wchar_t* text, int cx, int cy, UINT id) -> HWND {
        return CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            cx, cy, colW, rowH, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            hInstance_, nullptr);
    };

    auto label = [&](const wchar_t* text, int cx, int cy, int w) -> HWND {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
            cx, cy, w, rowH, hwnd_, nullptr, hInstance_, nullptr);
    };

    // Section: Chuyển đổi chữ
    label(L"Chuyển đổi chữ:", x, y, cw);
    y += rowH + gap;

    checkAllCaps_ = check(L"Chữ HOA", x, y, IDC_CHECK_ALL_CAPS);
    checkAllLower_ = check(L"Chữ thường", col2X, y, IDC_CHECK_ALL_LOWER);
    y += rowH + gap;

    checkCapsFirst_ = check(L"Hoa đầu câu", x, y, IDC_CHECK_CAPS_FIRST);
    checkCapsEach_ = check(L"Hoa Đầu Từ", col2X, y, IDC_CHECK_CAPS_EACH);
    y += rowH + gap;

    checkRemoveMark_ = check(L"Loại bỏ dấu", x, y, IDC_CHECK_REMOVE_MARK);
    y += rowH + gap * 2;

    // Section: Tuỳ chọn
    label(L"Tuỳ chọn:", x, y, cw);
    y += rowH + gap;

    checkAlertDone_ = check(L"Thông báo khi xong", x, y, IDC_CHECK_ALERT_DONE);
    checkAutoPaste_ = check(L"Tự động dán", col2X, y, IDC_CHECK_AUTO_PASTE);
    y += rowH + gap;

    checkSequential_ = check(L"Chuyển tuần tự", x, y, IDC_CHECK_SEQUENTIAL);
    checkEnableLog_  = check(L"Bật log debug", col2X, y, IDC_CHECK_ENABLE_LOG);
    y += rowH + gap * 2;

    // Section: Nguồn dữ liệu (Clipboard / File)
    label(L"Nguồn:", x, y, cw);
    y += rowH + gap;

    radioClipboard_ = CreateWindowExW(0, L"BUTTON", L"Clipboard",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
        x, y, colW, rowH, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RADIO_CLIPBOARD)),
        hInstance_, nullptr);
    radioFile_ = CreateWindowExW(0, L"BUTTON", L"Ch\x1ECDn file",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
        col2X, y, colW, rowH, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RADIO_FILE)),
        hInstance_, nullptr);
    CheckRadioButton(hwnd_, IDC_RADIO_CLIPBOARD, IDC_RADIO_FILE, IDC_RADIO_CLIPBOARD);
    y += rowH + gap;

    int browseW = Dpi(28);
    int labelW = Dpi(65);
    int editX = x + labelW + Dpi(4);  // gap between label and edit
    int pathW = cw - labelW - browseW - Dpi(4) - Dpi(8);
    int editH = theme_.ModernHeight();
    int labelY = y + (editH - rowH) / 2;

    labelSourceFile_ = CreateWindowExW(0, L"STATIC", L"File nguồn:",
        WS_CHILD | SS_LEFT, x, labelY, labelW, rowH,
        hwnd_, nullptr, hInstance_, nullptr);
    editSourcePath_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
        editX, y, pathW, editH,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_SOURCE_PATH)), hInstance_, nullptr);
    theme_.ApplyModernEntryStyle(editSourcePath_);
    btnBrowseSource_ = CreateWindowExW(0, L"BUTTON", L"...",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        editX + pathW + Dpi(8), y, browseW, editH,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_SOURCE)), hInstance_, nullptr);
    y += editH + gap;

    labelDestFile_ = CreateWindowExW(0, L"STATIC", L"File đích:",
        WS_CHILD | SS_LEFT, x, (y + (editH - rowH) / 2), labelW, rowH,
        hwnd_, nullptr, hInstance_, nullptr);
    editDestPath_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
        editX, y, pathW, editH,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_DEST_PATH)), hInstance_, nullptr);
    theme_.ApplyModernEntryStyle(editDestPath_);
    btnBrowseDest_ = CreateWindowExW(0, L"BUTTON", L"...",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        editX + pathW + Dpi(8), y, browseW, editH,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE_DEST)), hInstance_, nullptr);
    y += editH + gap * 2;

    // Section: Bảng mã
    labelEncodingSource_ = label(L"Bảng mã nguồn:", x, y + Dpi(4), Dpi(90));
    comboSource_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x + Dpi(90), y, cw - Dpi(90), Dpi(120),
        hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_SOURCE), hInstance_, nullptr);
    y += rowH + gap;

    labelEncodingDest_ = label(L"Bảng mã đích:", x, y + Dpi(4), Dpi(90));
    comboDest_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        x + Dpi(90), y, cw - Dpi(90), Dpi(120),
        hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_DEST), hInstance_, nullptr);

    // Populate encoding combos
    const wchar_t* encodings[] = {L"Unicode", L"TCVN3 (ABC)", L"VNI Windows", L"Unicode tổ hợp", L"Việt (CP 1258)"};
    for (auto& e : encodings) {
        ComboBox_AddString(comboSource_, e);
        ComboBox_AddString(comboDest_, e);
    }
    y += rowH + gap * 2;

    // Section: Phím tắt — single record button opens shared capture modal.
    labelHotkey_ = label(L"Phím tắt:", x, y, cw);
    y += rowH + gap;

    int clearBtnW = Dpi(64);
    int clearGap  = Dpi(6);
    int recordW   = cw - clearBtnW - clearGap;
    btnRecordHotkey_ = CreateWindowExW(0, L"BUTTON", L"— Chưa đặt —",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, recordW, rowH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_RECORD_HOTKEY), hInstance_, nullptr);
    btnClearHotkey_ = CreateWindowExW(0, L"BUTTON", L"Xóa",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + recordW + clearGap, y, clearBtnW, rowH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_CLEAR_HOTKEY), hInstance_, nullptr);
    y += rowH + gap * 3;

    // Action buttons
    int halfW = (cw - Dpi(8)) / 2;
    btnConvert_ = CreateWindowExW(0, L"BUTTON", L"Chuyển đổi",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        x, y, halfW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_CONVERT), hInstance_, nullptr);
    btnClose_ = CreateWindowExW(0, L"BUTTON", L"Đóng",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + halfW + Dpi(8), y, halfW, btnH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_CLOSE_DLG), hInstance_, nullptr);
}

void ClassicConvertToolDialog::PopulateFromConfig() {
    auto setCheck = [&](HWND h, bool v) {
        if (h) CheckDlgButton(hwnd_, GetDlgCtrlID(h), v ? BST_CHECKED : BST_UNCHECKED);
    };

    setCheck(checkAllCaps_, config_.allCaps);
    setCheck(checkAllLower_, config_.allLower);
    setCheck(checkCapsFirst_, config_.capsFirst);
    setCheck(checkCapsEach_, config_.capsEach);
    setCheck(checkRemoveMark_, config_.removeMark);
    setCheck(checkAlertDone_, config_.alertDone);
    setCheck(checkAutoPaste_, config_.autoPaste);
    setCheck(checkSequential_, config_.sequential);
    setCheck(checkEnableLog_, config_.enableLog);

    ComboBox_SetCurSel(comboSource_, config_.sourceEncoding);
    ComboBox_SetCurSel(comboDest_, config_.destEncoding);

    RefreshHotkeyDisplay();

    // Sequential only available when autoPaste is on
    EnableWindow(checkSequential_, config_.autoPaste ? TRUE : FALSE);
}

void ClassicConvertToolDialog::RefreshHotkeyDisplay() {
    if (!btnRecordHotkey_) return;
    std::wstring label = FormatHotkeyLabel(config_.hotkey.vk,
                                           config_.hotkey.ToMods());
    SetWindowTextW(btnRecordHotkey_,
                   label.empty() ? L"— Chưa đặt —" : label.c_str());
    if (btnClearHotkey_) {
        EnableWindow(btnClearHotkey_, config_.hotkey.HasAny() ? TRUE : FALSE);
    }
}

void ClassicConvertToolDialog::ReadToConfig() {
    auto isChecked = [&](UINT id) -> bool {
        return IsDlgButtonChecked(hwnd_, id) == BST_CHECKED;
    };

    config_.allCaps = isChecked(IDC_CHECK_ALL_CAPS);
    config_.allLower = isChecked(IDC_CHECK_ALL_LOWER);
    config_.capsFirst = isChecked(IDC_CHECK_CAPS_FIRST);
    config_.capsEach = isChecked(IDC_CHECK_CAPS_EACH);
    config_.removeMark = isChecked(IDC_CHECK_REMOVE_MARK);
    config_.alertDone = isChecked(IDC_CHECK_ALERT_DONE);
    config_.autoPaste = isChecked(IDC_CHECK_AUTO_PASTE);
    config_.sequential = isChecked(IDC_CHECK_SEQUENTIAL);
    config_.enableLog = isChecked(IDC_CHECK_ENABLE_LOG);

    int srcSel = ComboBox_GetCurSel(comboSource_);
    int dstSel = ComboBox_GetCurSel(comboDest_);
    if (srcSel >= 0) config_.sourceEncoding = static_cast<uint8_t>(srcSel);
    if (dstSel >= 0) config_.destEncoding = static_cast<uint8_t>(dstSel);

    // Hotkey config is written directly inside OpenCaptureModal() (see WndProc
    // below) — no UI controls to read here. Field stays at its previous value
    // for save flows triggered by other controls.
}

void ClassicConvertToolDialog::SaveConfig() {
    ReadToConfig();
    modified_ = true;
    (void)ConfigManager::SaveConvertConfig(ConfigManager::GetConfigPath(), config_);
    SignalConfigChange();
}

void ClassicConvertToolDialog::DoConvert() {
    SaveConfig();

    auto srcTable = static_cast<CodeTable>(config_.sourceEncoding);
    auto dstTable = static_cast<CodeTable>(config_.destEncoding);
    std::wstring input;

    if (fileMode_) {
        // ── File mode: read source file ──
        wchar_t srcPath[MAX_PATH] = {};
        GetWindowTextW(editSourcePath_, srcPath, MAX_PATH);
        if (srcPath[0] == 0) {
            MessageBoxW(hwnd_, S(StringId::CONVERT_NO_SOURCE_FILE), L"VKey", MB_OK | MB_ICONWARNING);
            return;
        }
        // Use ConvertToolDialog's static readFileContent (same logic)
        HANDLE hFile = CreateFileW(srcPath, GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            MessageBoxW(hwnd_, S(StringId::CONVERT_READ_ERROR), L"VKey", MB_OK | MB_ICONERROR);
            return;
        }
        DWORD fileSize = GetFileSize(hFile, nullptr);
        if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
            CloseHandle(hFile);
            MessageBoxW(hwnd_, S(StringId::CONVERT_READ_ERROR), L"VKey", MB_OK | MB_ICONERROR);
            return;
        }
        std::vector<BYTE> buffer(fileSize);
        DWORD bytesRead = 0;
        if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr)) {
            CloseHandle(hFile);
            MessageBoxW(hwnd_, S(StringId::CONVERT_READ_ERROR), L"VKey", MB_OK | MB_ICONERROR);
            return;
        }
        CloseHandle(hFile);

        if (srcTable == CodeTable::Unicode) {
            int wideLen = MultiByteToWideChar(CP_UTF8, 0,
                reinterpret_cast<const char*>(buffer.data()), static_cast<int>(bytesRead), nullptr, 0);
            if (wideLen > 0) {
                input.resize(wideLen);
                MultiByteToWideChar(CP_UTF8, 0,
                    reinterpret_cast<const char*>(buffer.data()), static_cast<int>(bytesRead),
                    input.data(), wideLen);
            }
        } else {
            input.reserve(bytesRead);
            for (DWORD i = 0; i < bytesRead; ++i)
                input += static_cast<wchar_t>(buffer[i]);
        }
    } else {
        // ── Clipboard mode ──
        if (!OpenClipboard(hwnd_)) {
            MessageBoxW(hwnd_, S(StringId::CONVERT_CLIPBOARD_EMPTY), L"VKey", MB_OK | MB_ICONWARNING);
            return;
        }
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (!hData) { CloseClipboard(); MessageBoxW(hwnd_, S(StringId::CONVERT_CLIPBOARD_EMPTY), L"VKey", MB_OK | MB_ICONWARNING); return; }
        auto* pText = static_cast<const wchar_t*>(GlobalLock(hData));
        if (!pText) { CloseClipboard(); MessageBoxW(hwnd_, S(StringId::CONVERT_CLIPBOARD_EMPTY), L"VKey", MB_OK | MB_ICONWARNING); return; }
        input = pText;
        GlobalUnlock(hData);
        CloseClipboard();
    }

    if (input.empty()) {
        MessageBoxW(hwnd_,
            S(fileMode_ ? StringId::CONVERT_READ_ERROR : StringId::CONVERT_CLIPBOARD_EMPTY),
            L"VKey", MB_OK | MB_ICONWARNING);
        return;
    }

    // Decode → transform → encode
    std::wstring unicode = CodeTableConverter::DecodeString(input, srcTable);
    if (config_.removeMark) unicode = CodeTableConverter::RemoveDiacritics(unicode);
    if (config_.allCaps) unicode = CodeTableConverter::ToUpper(unicode);
    else if (config_.allLower) unicode = CodeTableConverter::ToLower(unicode);
    else if (config_.capsFirst) unicode = CodeTableConverter::ToSentenceCase(unicode);
    else if (config_.capsEach) unicode = CodeTableConverter::ToTitleCase(unicode);
    std::wstring output = CodeTableConverter::EncodeString(unicode, dstTable);

    if (fileMode_) {
        // ── Write to dest file ──
        wchar_t dstPath[MAX_PATH] = {};
        GetWindowTextW(editDestPath_, dstPath, MAX_PATH);
        if (dstPath[0] == 0) {
            MessageBoxW(hwnd_, S(StringId::CONVERT_NO_DEST_FILE), L"VKey", MB_OK | MB_ICONWARNING);
            return;
        }
        HANDLE hFile = CreateFileW(dstPath, GENERIC_WRITE, 0,
                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            MessageBoxW(hwnd_, S(StringId::CONVERT_WRITE_ERROR), L"VKey", MB_OK | MB_ICONERROR);
            return;
        }
        DWORD bytesWritten = 0;
        bool ok = false;
        if (dstTable == CodeTable::Unicode) {
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, output.c_str(), static_cast<int>(output.size()),
                                              nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                std::vector<char> utf8(utf8Len);
                WideCharToMultiByte(CP_UTF8, 0, output.c_str(), static_cast<int>(output.size()),
                                    utf8.data(), utf8Len, nullptr, nullptr);
                ok = WriteFile(hFile, utf8.data(), utf8Len, &bytesWritten, nullptr) != FALSE;
            }
        } else {
            std::vector<BYTE> bytes;
            bytes.reserve(output.size());
            for (wchar_t ch : output) bytes.push_back(static_cast<BYTE>(ch & 0xFF));
            ok = WriteFile(hFile, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, nullptr) != FALSE;
        }
        CloseHandle(hFile);
        if (!ok) {
            MessageBoxW(hwnd_, S(StringId::CONVERT_WRITE_ERROR), L"VKey", MB_OK | MB_ICONERROR);
            return;
        }
    } else {
        // ── Write to clipboard ──
        if (!OpenClipboard(hwnd_)) {
            MessageBoxW(hwnd_, S(StringId::CONVERT_CLIPBOARD_WRITE_ERROR), L"VKey", MB_OK | MB_ICONERROR);
            return;
        }
        EmptyClipboard();
        size_t bytes = (output.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!hMem) { CloseClipboard(); return; }
        auto* pDst = static_cast<wchar_t*>(GlobalLock(hMem));
        if (pDst) {
            memcpy(pDst, output.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }

    if (config_.alertDone) {
        MessageBoxW(hwnd_, S(StringId::CONVERT_SUCCESS), L"VKey", MB_OK | MB_ICONINFORMATION);
    }
}

void ClassicConvertToolDialog::BrowseFile(bool isSource) {
    WCHAR szFile[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0"
                      L"Rich Text Format (*.rtf)\0*.rtf\0"
                      L"All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;

    BOOL result = FALSE;
    if (isSource) {
        ofn.lpstrTitle = L"Ch\x1ECDn file ngu\x1ED3n";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        result = GetOpenFileNameW(&ofn);
    } else {
        ofn.lpstrTitle = L"Ch\x1ECDn file \x0111\x00EDch";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        result = GetSaveFileNameW(&ofn);
    }
    if (!result) return;

    SetWindowTextW(isSource ? editSourcePath_ : editDestPath_, szFile);
}

void ClassicConvertToolDialog::UpdateFileMode() {
    fileMode_ = (IsDlgButtonChecked(hwnd_, IDC_RADIO_FILE) == BST_CHECKED);
    int show = fileMode_ ? SW_SHOW : SW_HIDE;
    ShowWindow(labelSourceFile_, show);
    ShowWindow(editSourcePath_, show);
    ShowWindow(btnBrowseSource_, show);
    ShowWindow(labelDestFile_, show);
    ShowWindow(editDestPath_, show);
    ShowWindow(btnBrowseDest_, show);

    int x = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int rowH = Dpi(kRowH);
    int gap = Dpi(kRowGap);

    RECT rcRadio;
    GetWindowRect(radioClipboard_, &rcRadio);
    MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<LPPOINT>(&rcRadio), 2);
    int y = rcRadio.bottom + gap;

    if (fileMode_) {
        // Accounting for the 2 rows of file inputs that are now visible
        y += rowH + gap + Dpi(4);
        y += rowH + gap * 2 + Dpi(4);
    }

    HDWP hdwp = BeginDeferWindowPos(14);
    
    hdwp = DeferWindowPos(hdwp, labelEncodingSource_, nullptr, x, y + Dpi(4), Dpi(90), rowH, SWP_NOZORDER | SWP_NOSIZE);
    hdwp = DeferWindowPos(hdwp, comboSource_, nullptr, x + Dpi(90), y, cw - Dpi(90), Dpi(120), SWP_NOZORDER | SWP_NOSIZE);
    y += rowH + gap;

    hdwp = DeferWindowPos(hdwp, labelEncodingDest_, nullptr, x, y + Dpi(4), Dpi(90), rowH, SWP_NOZORDER | SWP_NOSIZE);
    hdwp = DeferWindowPos(hdwp, comboDest_, nullptr, x + Dpi(90), y, cw - Dpi(90), Dpi(120), SWP_NOZORDER | SWP_NOSIZE);
    y += rowH + gap * 2;

    hdwp = DeferWindowPos(hdwp, labelHotkey_,     nullptr, x, y, cw, rowH,
                          SWP_NOZORDER | SWP_NOSIZE);
    y += rowH + gap;
    int clearBtnW = Dpi(64);
    int clearGap  = Dpi(6);
    int recordW   = cw - clearBtnW - clearGap;
    hdwp = DeferWindowPos(hdwp, btnRecordHotkey_, nullptr, x, y, recordW, rowH,
                          SWP_NOZORDER);
    hdwp = DeferWindowPos(hdwp, btnClearHotkey_,  nullptr, x + recordW + clearGap, y,
                          clearBtnW, rowH, SWP_NOZORDER);
    y += rowH + gap * 3;

    int halfW = (cw - Dpi(8)) / 2;
    hdwp = DeferWindowPos(hdwp, btnConvert_, nullptr, x, y, halfW, Dpi(kBtnHeight), SWP_NOZORDER | SWP_NOSIZE);
    hdwp = DeferWindowPos(hdwp, btnClose_, nullptr, x + halfW + Dpi(8), y, halfW, Dpi(kBtnHeight), SWP_NOZORDER | SWP_NOSIZE);
    y += Dpi(kBtnHeight) + Dpi(kPadding);

    EndDeferWindowPos(hdwp);

    DWORD style = GetWindowLongW(hwnd_, GWL_STYLE);
    DWORD exStyle = GetWindowLongW(hwnd_, GWL_EXSTYLE);
    RECT rcClient = {0, 0, Dpi(kWidth), y};
    AdjustWindowRectEx(&rcClient, style, FALSE, exStyle);
    SetWindowPos(hwnd_, nullptr, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

int ClassicConvertToolDialog::Dpi(int value) const noexcept {
    return Classic::DpiScale(value, dpi_);
}

// ════════════════════════════════════════════════════════════

LRESULT CALLBACK ClassicConvertToolDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) try {
    ClassicConvertToolDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicConvertToolDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicConvertToolDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            UINT code = HIWORD(wParam);

            switch (id) {
                case IDC_BTN_CONVERT:        self->DoConvert();        return 0;
                case IDC_BTN_CLOSE_DLG:      DestroyWindow(hwnd);     return 0;
                case IDC_BTN_BROWSE_SOURCE:  self->BrowseFile(true);  return 0;
                case IDC_BTN_BROWSE_DEST:    self->BrowseFile(false); return 0;
                case IDC_RADIO_CLIPBOARD:
                case IDC_RADIO_FILE:         self->UpdateFileMode();  return 0;
                case IDC_BTN_RECORD_HOTKEY: {
                    // Modal capture — combo-only (allowDoubleTap=false,
                    // allowBareModifier=false) since the convert HotkeyManager
                    // slot can't fire on 2×Alt or "Ctrl alone".
                    HotkeyCaptureOptions opts{
                        .allowDoubleTap    = false,
                        .allowBareModifier = false,
                        .titleText  = L"Ghi nhận phím chuyển mã",
                        .promptText = L"Nhấn phím tắt chuyển mã (kèm Ctrl/Shift/Alt/Win). F1-F12 đều dùng được.",
                    };
                    if (auto r = ShowHotkeyCaptureDialog(
                            self->hInstance_, hwnd, self->theme_, self->dpi_, opts)) {
                        self->config_.hotkey.vk = r->vk;
                        self->config_.hotkey.SetModsFromMask(r->mods);
                        self->RefreshHotkeyDisplay();
                        self->SaveConfig();
                    }
                    return 0;
                }
                case IDC_BTN_CLEAR_HOTKEY: {
                    if (!self->config_.hotkey.HasAny()) return 0;
                    self->config_.hotkey = HotkeyConfig{};
                    self->RefreshHotkeyDisplay();
                    self->SaveConfig();
                    return 0;
                }
            }

            // Auto-save on any toggle/combo change
            if (code == BN_CLICKED || code == CBN_SELCHANGE) {
                self->SaveConfig();

                // Enable/disable sequential based on autoPaste
                if (id == IDC_CHECK_AUTO_PASTE) {
                    bool ap = IsDlgButtonChecked(hwnd, IDC_CHECK_AUTO_PASTE) == BST_CHECKED;
                    EnableWindow(self->checkSequential_, ap ? TRUE : FALSE);
                }
            }
            break;
        }

        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, self->theme_.BrushBackground());
            return 1;
        }

        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorStatic(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND hctrl = reinterpret_cast<HWND>(lParam);
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorEdit(hdc, hctrl));
        }
        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorListBox(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorBtn(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

        case WM_CLOSE:
            self->SaveConfig();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            self->theme_.Destroy();
            self->hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
} catch (const std::exception& e) {
    NextKey::CrashLog(L"ClassicConvertToolDialog::WndProc", e.what());
    return DefWindowProcW(hwnd, msg, wParam, lParam);
} catch (...) {
    NextKey::CrashLog(L"ClassicConvertToolDialog::WndProc", "(non-std exception)");
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey::Classic
