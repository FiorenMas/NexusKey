// NexusKey Classic — Excluded Apps Dialog Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicExcludedAppsDialog.h"
#include "core/config/ConfigManager.h"
#include "core/CrashLog.h"
#include "app/helpers/AppHelpers.h"

#include <windowsx.h>
#include <algorithm>
#include <exception>
#include <sstream>
#include <fstream>

namespace NextKey::Classic {

// Control IDs
enum {
    IDC_LIST_APPS = 3001,
    IDC_COMBO_RUNNING,
    IDC_BTN_ADD,
    IDC_BTN_PICK,
    IDC_BTN_DELETE,
    IDC_BTN_IMPORT,
    IDC_BTN_EXPORT,
};

// ════════════════════════════════════════════════════════════
// Public entry point
// ════════════════════════════════════════════════════════════

bool ClassicExcludedAppsDialog::Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
    ClassicExcludedAppsDialog dlg;
    if (!dlg.Init(hInstance, parent, forceLightTheme)) return false;

    // Modal message loop
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
// Initialization
// ════════════════════════════════════════════════════════════

bool ClassicExcludedAppsDialog::Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
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
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Loại trừ ứng dụng",
        style, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    // DPI
    dpi_ = Classic::GetWindowDpi(hwnd_);

    // Resize + center
    int w = Dpi(kWidth), h = Dpi(kHeight);
    RECT rc = {0, 0, w, h};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_TOPMOST);
    int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd_, nullptr, (sx - aw) / 2, (sy - ah) / 2, aw, ah, SWP_NOZORDER);

    theme_.Init(hwnd_, forceLightTheme);
    theme_.ApplyWindowAttributes(hwnd_);

    LoadData();
    CreateControls();
    PopulateList();

    // Apply theme + font
    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicExcludedAppsDialog*>(lp);
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

void ClassicExcludedAppsDialog::CreateControls() {
    int x = Dpi(kPadding), y = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int btnH = Dpi(kBtnHeight);
    int gap = Dpi(kBtnGap);

    // ListView
    int listH = Dpi(200);
    listView_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
        x, y, cw, listH, hwnd_, reinterpret_cast<HMENU>(IDC_LIST_APPS), hInstance_, nullptr);
    ListView_SetExtendedListViewStyle(listView_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"Ứng dụng (tên .exe)");
    col.cx = cw - Dpi(24);
    ListView_InsertColumn(listView_, 0, &col);
    y += listH + gap;

    // Row: combo running apps + add from list + pick window
    int comboW = cw - Dpi(60 + 80) - gap * 2;
    comboRunning_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP,
        x, y, comboW, Dpi(200), hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_RUNNING), hInstance_, nullptr);

    // Populate running apps
    auto running = GetRunningApps();
    for (auto& app : running) {
        ComboBox_AddString(comboRunning_, app.c_str());
    }

    btnAdd_ = CreateWindowExW(0, L"BUTTON", L"Thêm",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + comboW + gap, y, Dpi(60), btnH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_ADD), hInstance_, nullptr);

    btnPick_ = CreateWindowExW(0, L"BUTTON", L"Chọn cửa sổ",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + comboW + Dpi(60) + gap * 2, y, Dpi(80), btnH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_PICK), hInstance_, nullptr);
    y += btnH + gap * 2;

    // Action buttons row
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

void ClassicExcludedAppsDialog::PopulateList() {
    ListView_DeleteAllItems(listView_);
    for (size_t i = 0; i < appList_.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(appList_[i].c_str());
        ListView_InsertItem(listView_, &item);
    }
}

// ════════════════════════════════════════════════════════════
// Actions
// ════════════════════════════════════════════════════════════

void ClassicExcludedAppsDialog::AddApp(const std::wstring& name) {
    if (name.empty()) return;

    std::wstring lower = ToLowerAscii(name);

    // Block NexusKey itself
    if (lower == L"nexuskey.exe" || lower == L"nexuskeylite.exe") {
        MessageBoxW(hwnd_, L"Không thể thêm NexusKey vào danh sách loại trừ.",
            L"Lỗi", MB_ICONWARNING);
        return;
    }

    // Dedup
    for (auto& existing : appList_) {
        if (existing == lower) return;
    }

    appList_.push_back(lower);
    std::sort(appList_.begin(), appList_.end());
    PopulateList();
    SaveData();
}

void ClassicExcludedAppsDialog::DeleteSelected() {
    int sel = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(appList_.size())) return;

    appList_.erase(appList_.begin() + sel);
    PopulateList();
    SaveData();
}

void ClassicExcludedAppsDialog::ImportFromFile() {
    auto path = OpenFileDialog(hwnd_,
        L"Text Files (*.txt)\0*.txt\0All Files\0*.*\0",
        L"Nhập danh sách ứng dụng");
    if (path.empty()) return;

    // Ask replace or append
    int choice = MessageBoxW(hwnd_,
        L"Thay thế danh sách hiện tại hay thêm vào?",
        L"Nhập file",
        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return;

    std::ifstream file(path);
    if (!file.is_open()) {
        MessageBoxW(hwnd_, L"Không thể mở file.", L"Lỗi", MB_ICONERROR);
        return;
    }

    if (choice == IDYES) appList_.clear(); // Replace

    std::string line;
    while (std::getline(file, line)) {
        // Trim CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;

        std::wstring wide = Utf8ToWide(line);
        std::wstring lower = ToLowerAscii(wide);
        if (!lower.empty()) {
            bool dup = false;
            for (auto& e : appList_) { if (e == lower) { dup = true; break; } }
            if (!dup) appList_.push_back(lower);
        }
    }

    std::sort(appList_.begin(), appList_.end());
    PopulateList();
    SaveData();
}

void ClassicExcludedAppsDialog::ExportToFile() {
    auto path = SaveFileDialog(hwnd_,
        L"Text Files (*.txt)\0*.txt\0",
        L"Xuất danh sách ứng dụng", L"txt");
    if (path.empty()) return;

    std::ofstream file(path);
    if (!file.is_open()) {
        MessageBoxW(hwnd_, L"Không thể tạo file.", L"Lỗi", MB_ICONERROR);
        return;
    }

    file << ";NexusKey Excluded Apps\n";
    auto sorted = appList_;
    std::sort(sorted.begin(), sorted.end());
    for (auto& app : sorted) {
        file << WideToUtf8(app) << "\n";
    }
}

void ClassicExcludedAppsDialog::OnPickWindow() {
    picker_.Start(hwnd_, [this](const std::wstring& exeName) {
        SetWindowTextW(comboRunning_, exeName.c_str());
    });
}

// ════════════════════════════════════════════════════════════
// Data I/O
// ════════════════════════════════════════════════════════════

void ClassicExcludedAppsDialog::LoadData() {
    appList_ = ConfigManager::LoadAllExcludedApps(ConfigManager::GetConfigPath());
}

void ClassicExcludedAppsDialog::SaveData() {
    modified_ = true;
    (void)ConfigManager::SaveExcludedApps(ConfigManager::GetConfigPath(), appList_);
    SignalConfigChange();
}

// ════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════

int ClassicExcludedAppsDialog::Dpi(int value) const noexcept {
    return Classic::DpiScale(value, dpi_);
}

// ════════════════════════════════════════════════════════════
// Window procedure
// ════════════════════════════════════════════════════════════

LRESULT CALLBACK ClassicExcludedAppsDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) try {
    ClassicExcludedAppsDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicExcludedAppsDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicExcludedAppsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    // Window picker intercepts
    if (self->picker_.HandleMessage(msg, wParam)) return 0;

    switch (msg) {
        case WM_COMMAND: {
            UINT id = LOWORD(wParam);

            switch (id) {
                case IDC_BTN_ADD: {
                    wchar_t buf[256] = {};
                    GetWindowTextW(self->comboRunning_, buf, 256);
                    self->AddApp(buf);
                    SetWindowTextW(self->comboRunning_, L"");
                    SetFocus(self->comboRunning_);
                    return 0;
                }
                case IDC_BTN_PICK:
                    self->OnPickWindow();
                    return 0;
                case IDC_BTN_DELETE:
                    self->DeleteSelected();
                    return 0;
                case IDC_BTN_IMPORT:
                    self->ImportFromFile();
                    return 0;
                case IDC_BTN_EXPORT:
                    self->ExportToFile();
                    return 0;
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
    NextKey::CrashLog(L"ClassicExcludedAppsDialog::WndProc", e.what());
    return DefWindowProcW(hwnd, msg, wParam, lParam);
} catch (...) {
    NextKey::CrashLog(L"ClassicExcludedAppsDialog::WndProc", "(non-std exception)");
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace NextKey::Classic
