// NexusKey Classic — Macro Table Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicMacroTableDialog.h"
#include "core/config/ConfigManager.h"
#include "core/CrashLog.h"
#include "app/helpers/AppHelpers.h"

#include <windowsx.h>
#include <algorithm>
#include <exception>
#include <fstream>
#include <vector>

namespace NextKey::Classic {

enum {
    IDC_LIST_MACROS = 3101,
    IDC_EDIT_KEY,
    IDC_EDIT_VALUE,
    IDC_BTN_ADD,
    IDC_BTN_DELETE,
    IDC_BTN_IMPORT,
    IDC_BTN_EXPORT,
    IDC_CHK_SPACE,
    IDC_CHK_ENTER,
    IDC_CHK_TAB,
    IDC_CHK_DIR
};

// ════════════════════════════════════════════════════════════
// Public
// ════════════════════════════════════════════════════════════

bool ClassicMacroTableDialog::Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
    ClassicMacroTableDialog dlg;
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

// ════════════════════════════════════════════════════════════
// Init
// ════════════════════════════════════════════════════════════

bool ClassicMacroTableDialog::Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
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
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Bảng gõ tắt",
        style, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    dpi_ = Classic::GetWindowDpi(hwnd_);

    int w = Dpi(kWidth), h = Dpi(kHeight);
    RECT rc = {0, 0, w, h};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_TOPMOST);
    int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd_, nullptr, (sx - aw) / 2, (sy - ah) / 2, aw, ah, SWP_NOZORDER);

    theme_.Init(hwnd_, forceLightTheme);
    theme_.ApplyWindowAttributes(hwnd_);

    CreateControls();
    LoadData();
    PopulateList();

    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicMacroTableDialog*>(lp);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(self->theme_.Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    theme_.ThemeAllChildren(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

// ════════════════════════════════════════════════════════════
// Controls
// ════════════════════════════════════════════════════════════

void ClassicMacroTableDialog::CreateControls() {
    int x = Dpi(kPadding), y = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int btnH = Dpi(kBtnHeight);
    int gap = Dpi(kBtnGap);

    // ListView — 2 columns: Key, Expansion
    int listH = Dpi(200);
    listView_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
        x, y, cw, listH, hwnd_, reinterpret_cast<HMENU>(IDC_LIST_MACROS), hInstance_, nullptr);
    ListView_SetExtendedListViewStyle(listView_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"Phím tắt");
    col.cx = Dpi(100);
    ListView_InsertColumn(listView_, 0, &col);

    col.pszText = const_cast<wchar_t*>(L"Nội dung");
    col.cx = cw - Dpi(100 + 24);
    ListView_InsertColumn(listView_, 1, &col);
    y += listH + gap;

    // Row: key edit + add button
    int editH = theme_.ModernHeight();
    int keyW = Dpi(100);
    int addW = Dpi(50);

    editKey_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, keyW, editH, hwnd_, reinterpret_cast<HMENU>(IDC_EDIT_KEY), hInstance_, nullptr);
    SendMessageW(editKey_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"btv"));
    SendMessageW(editKey_, EM_SETLIMITTEXT, 32, 0);
    theme_.ApplyModernEntryStyle(editKey_);

    btnAdd_ = CreateWindowExW(0, L"BUTTON", L"Thêm",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + cw - addW, y, addW, editH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_ADD), hInstance_, nullptr);
    y += editH + gap;

    // Multi-line value edit (full width, 4 lines)
    int valH = Dpi(80);
    editValue_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        x, y, cw, valH, hwnd_, reinterpret_cast<HMENU>(IDC_EDIT_VALUE), hInstance_, nullptr);
    SendMessageW(editValue_, EM_SETLIMITTEXT, 20480, 0);
    theme_.ApplyModernEntryStyle(editValue_);
    y += valH + gap;

    // Trigger Checkboxes
    int chkY = y;
    int chkH = Dpi(20);
    int lblW = Dpi(100);
    lblTrigger_ = CreateWindowExW(0, L"STATIC", L"Phím kích hoạt:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, chkY, lblW, chkH, hwnd_, nullptr, hInstance_, nullptr);

    int cbX = x + lblW - Dpi(4); // slightly closer to label
    
    int wSpace = Dpi(60);
    chkSpace_ = CreateWindowExW(0, L"BUTTON", L"Space",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        cbX, chkY, wSpace, chkH, hwnd_, reinterpret_cast<HMENU>(IDC_CHK_SPACE), hInstance_, nullptr);
    cbX += wSpace;

    int wEnter = Dpi(55);
    chkEnter_ = CreateWindowExW(0, L"BUTTON", L"Enter",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        cbX, chkY, wEnter, chkH, hwnd_, reinterpret_cast<HMENU>(IDC_CHK_ENTER), hInstance_, nullptr);
    cbX += wEnter;

    int wTab = Dpi(45);
    chkTab_ = CreateWindowExW(0, L"BUTTON", L"Tab",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        cbX, chkY, wTab, chkH, hwnd_, reinterpret_cast<HMENU>(IDC_CHK_TAB), hInstance_, nullptr);
    cbX += wTab;

    int wDir = Dpi(90);
    chkDir_ = CreateWindowExW(0, L"BUTTON", L"Điều hướng",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        cbX, chkY, wDir, chkH, hwnd_, reinterpret_cast<HMENU>(IDC_CHK_DIR), hInstance_, nullptr);
    
    y = chkY + chkH + gap * 2;

    // Action buttons
    int abw = (cw - gap * 2) / 3;
    btnDelete_ = CreateWindowExW(0, L"BUTTON", L"Xoá",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, abw, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_DELETE), hInstance_, nullptr);
    btnImport_ = CreateWindowExW(0, L"BUTTON", L"Nhập file",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + abw + gap, y, abw, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_IMPORT), hInstance_, nullptr);
    btnExport_ = CreateWindowExW(0, L"BUTTON", L"Xuất file",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + (abw + gap) * 2, y, abw, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_EXPORT), hInstance_, nullptr);
    y += btnH + gap * 2;
}

// Convert storage format (\n literal 2-char) → \r\n for Win32 multi-line EDIT
std::wstring ClassicMacroTableDialog::StorageToEdit(const std::wstring& s) {
    std::wstring result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size() && s[i + 1] == L'n') {
            result += L"\r\n";
            ++i;
        } else {
            result += s[i];
        }
    }
    return result;
}

// Convert \r\n from Win32 EDIT → storage format (\n literal 2-char)
std::wstring ClassicMacroTableDialog::EditToStorage(const std::wstring& s) {
    std::wstring result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') {
            result += L"\\n";
            ++i;
        } else if (s[i] == L'\n') {
            result += L"\\n";
        } else {
            result += s[i];
        }
    }
    return result;
}

// Read text from multi-line EDIT using dynamic buffer
std::wstring ClassicMacroTableDialog::GetEditValue() const {
    int len = GetWindowTextLengthW(editValue_);
    if (len <= 0) return {};
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(editValue_, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(len));
    return buf;
}

void ClassicMacroTableDialog::PopulateList() {
    ListView_DeleteAllItems(listView_);

    std::vector<std::pair<std::wstring, std::wstring>> sorted(macros_.begin(), macros_.end());
    std::sort(sorted.begin(), sorted.end());

    for (int i = 0; i < static_cast<int>(sorted.size()); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(sorted[i].first.c_str());
        ListView_InsertItem(listView_, &item);

        // Preview: first line, truncated, with line count
        const auto& val = sorted[i].second;
        std::wstring preview;
        auto nlPos = val.find(L"\\n");
        int lineCount = 1;
        {
            size_t pos = 0;
            while ((pos = val.find(L"\\n", pos)) != std::wstring::npos) {
                ++lineCount;
                pos += 2;
            }
        }
        std::wstring firstLine = (nlPos != std::wstring::npos) ? val.substr(0, nlPos) : val;
        if (firstLine.size() > 60) firstLine = firstLine.substr(0, 60) + L"...";
        if (lineCount > 1) {
            preview = firstLine + L" \u23CE" + std::to_wstring(lineCount);
        } else {
            preview = firstLine;
        }
        ListView_SetItemText(listView_, i, 1, const_cast<wchar_t*>(preview.c_str()));
    }
}

// ════════════════════════════════════════════════════════════
// Actions
// ════════════════════════════════════════════════════════════

void ClassicMacroTableDialog::AddMacro() {
    wchar_t key[64] = {};
    GetWindowTextW(editKey_, key, 64);

    std::wstring k(key);
    std::wstring editText = GetEditValue();
    std::wstring v = EditToStorage(editText);

    if (k.empty() || v.empty()) return;

    macros_[k] = v;
    PopulateList();
    SaveData();

    SetWindowTextW(editKey_, L"");
    SetWindowTextW(editValue_, L"");
    SetFocus(editKey_);
}

void ClassicMacroTableDialog::DeleteSelected() {
    int sel = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (sel < 0) return;

    wchar_t key[64] = {};
    ListView_GetItemText(listView_, sel, 0, key, 64);
    macros_.erase(key);
    PopulateList();
    SaveData();
}

void ClassicMacroTableDialog::ImportFromFile() {
    auto path = OpenFileDialog(hwnd_,
        L"Text Files (*.txt)\0*.txt\0All Files\0*.*\0",
        L"Nhập bảng gõ tắt");
    if (path.empty()) return;

    int choice = MessageBoxW(hwnd_,
        L"Thay thế bảng hiện tại hay thêm vào?",
        L"Nhập file",
        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return;

    std::ifstream file(path);
    if (!file.is_open()) {
        MessageBoxW(hwnd_, L"Không thể mở file.", L"Lỗi", MB_ICONERROR);
        return;
    }

    if (choice == IDYES) macros_.clear();

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;

        auto colonPos = line.find(':');
        if (colonPos == std::string::npos || colonPos == 0) continue;

        std::wstring k = Utf8ToWide(line.substr(0, colonPos));
        std::wstring v = Utf8ToWide(line.substr(colonPos + 1));
        if (!k.empty() && !v.empty() && k.size() <= 32 && v.size() <= 20480) {
            macros_[k] = v;
        }
    }

    PopulateList();
    SaveData();
}

void ClassicMacroTableDialog::ExportToFile() {
    auto path = SaveFileDialog(hwnd_,
        L"Text Files (*.txt)\0*.txt\0",
        L"Xuất bảng gõ tắt", L"txt");
    if (path.empty()) return;

    std::ofstream file(path);
    if (!file.is_open()) {
        MessageBoxW(hwnd_, L"Không thể tạo file.", L"Lỗi", MB_ICONERROR);
        return;
    }

    file << ";Compatible OpenKey Macro Data file*** version=1 ***\n";
    std::vector<std::pair<std::wstring, std::wstring>> sorted(macros_.begin(), macros_.end());
    std::sort(sorted.begin(), sorted.end());
    for (auto& [k, v] : sorted) {
        file << WideToUtf8(k) << ":" << WideToUtf8(v) << "\n";
    }
}

// ════════════════════════════════════════════════════════════
// Data I/O
// ════════════════════════════════════════════════════════════

void ClassicMacroTableDialog::LoadData() {
    macros_ = ConfigManager::LoadMacros(ConfigManager::GetConfigPath());
    
    TypingConfig cfg = ConfigManager::LoadOrDefault();
    SendMessageW(chkSpace_, BM_SETCHECK, cfg.macroTriggerSpace ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chkEnter_, BM_SETCHECK, cfg.macroTriggerEnter ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chkTab_, BM_SETCHECK, cfg.macroTriggerTab ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(chkDir_, BM_SETCHECK, cfg.macroTriggerDir ? BST_CHECKED : BST_UNCHECKED, 0);
}

void ClassicMacroTableDialog::SaveData() {
    modified_ = true;
    (void)ConfigManager::SaveMacros(ConfigManager::GetConfigPath(), macros_);
    SignalConfigChange();
}

void ClassicMacroTableDialog::SaveTriggerConfig() {
    TypingConfig cfg = ConfigManager::LoadOrDefault();
    cfg.macroTriggerSpace = SendMessageW(chkSpace_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.macroTriggerEnter = SendMessageW(chkEnter_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.macroTriggerTab = SendMessageW(chkTab_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.macroTriggerDir = SendMessageW(chkDir_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    (void)ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), cfg);
    SignalConfigChange();
}

int ClassicMacroTableDialog::Dpi(int value) const noexcept {
    return Classic::DpiScale(value, dpi_);
}

// ════════════════════════════════════════════════════════════
// WndProc
// ════════════════════════════════════════════════════════════

LRESULT CALLBACK ClassicMacroTableDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) try {
    ClassicMacroTableDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicMacroTableDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicMacroTableDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            switch (id) {
                case IDC_BTN_ADD:     self->AddMacro();       return 0;
                case IDC_BTN_DELETE:  self->DeleteSelected(); return 0;
                case IDC_BTN_IMPORT:  self->ImportFromFile(); return 0;
                case IDC_BTN_EXPORT:  self->ExportToFile();   return 0;
                case IDC_CHK_SPACE:
                case IDC_CHK_ENTER:
                case IDC_CHK_TAB:
                case IDC_CHK_DIR:
                    if (HIWORD(wParam) == BN_CLICKED) self->SaveTriggerConfig();
                    return 0;
            }
            break;
        }

        case WM_NOTIFY: {
            auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
            if (nmhdr->idFrom == IDC_LIST_MACROS && nmhdr->code == LVN_ITEMCHANGED) {
                auto* nmItem = reinterpret_cast<NMLISTVIEW*>(lParam);
                if (nmItem->uNewState & LVIS_SELECTED) {
                    int sel = nmItem->iItem;
                    wchar_t key[64] = {};
                    ListView_GetItemText(self->listView_, sel, 0, key, 64);
                    SetWindowTextW(self->editKey_, key);
                    auto it = self->macros_.find(key);
                    if (it != self->macros_.end()) {
                        auto editText = ClassicMacroTableDialog::StorageToEdit(it->second);
                        SetWindowTextW(self->editValue_, editText.c_str());
                    }
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
        case WM_CTLCOLOREDIT:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorEdit(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorListBox(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(self->theme_.OnCtlColorBtn(
                reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

        case WM_CLOSE:
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
    NextKey::CrashLog(L"ClassicMacroTableDialog::WndProc", e.what());
    return DefWindowProcW(hwnd, msg, wParam, lParam);
} catch (...) {
    NextKey::CrashLog(L"ClassicMacroTableDialog::WndProc", "(non-std exception)");
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey::Classic
