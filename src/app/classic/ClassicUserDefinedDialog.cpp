// VKey Classic — User Defined Input Dialog Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "ClassicUserDefinedDialog.h"
#include "core/config/ConfigManager.h"
#include "app/helpers/AppHelpers.h"

#include <windowsx.h>
#include <vector>

namespace NextKey::Classic {

enum {
    IDC_LIST_KEYMAP = 3201,
    IDC_EDIT_KEY,
    IDC_COMBO_ACTION,
    IDC_BTN_APPLY,
    IDC_BTN_CLEAR,
    IDC_BTN_LOAD_TELEX,
    IDC_BTN_LOAD_VNI,
    IDC_BTN_IMPORT,
    IDC_BTN_EXPORT
};

// Vietnamese labels for the action dropdown + listview. Mirror of
// userdefined.html <option> text — keep in sync if labels change there.
// Classic UI is Vietnamese-only by design (no i18n runtime).
namespace {
[[nodiscard]] const wchar_t* ActionLabel(TypingAction a) noexcept {
    switch (a) {
        case TypingAction::None: return L"";
        case TypingAction::ClearTone: return L"Xoá dấu";
        case TypingAction::ToneAcute: return L"Dấu Sắc";
        case TypingAction::ToneGrave: return L"Dấu Huyền";
        case TypingAction::ToneHook: return L"Dấu Hỏi";
        case TypingAction::ToneTilde: return L"Dấu Ngã";
        case TypingAction::ToneDot: return L"Dấu Nặng";
        case TypingAction::VniCircumflex: return L"Mũ chung (â,ê,ô)";
        case TypingAction::CircumflexA: return L"Mũ cho a → â";
        case TypingAction::CircumflexE: return L"Mũ cho e → ê";
        case TypingAction::CircumflexO: return L"Mũ cho o → ô";
        case TypingAction::HornW: return L"Móc chung (ă,ư,ơ)";
        case TypingAction::HornInsertU: return L"Móc cho u → ư";
        case TypingAction::HornInsertO: return L"Móc cho o → ơ";
        case TypingAction::VniHorn: return L"Móc chung VNI 7 → ư,ơ";
        case TypingAction::VniBreve: return L"Trăng cho a → ă";
        case TypingAction::HornOrInsertU: return L"Móc hoặc chữ ư";
        case TypingAction::HornOrInsertUNoStart: return L"Móc hoặc ư (trừ đầu từ)";
        case TypingAction::StrokeD: return L"Gạch d → đ";
        case TypingAction::VniStroke: return L"Gạch chung (VNI 9)";
        case TypingAction::UndoAllMarks: return L"Thoát bỏ dấu";
        case TypingAction::InsertABreve: return L"Chữ ă";
        case TypingAction::InsertABreveUpper: return L"Chữ Ă";
        case TypingAction::InsertACircumflex: return L"Chữ â";
        case TypingAction::InsertACircumflexUpper: return L"Chữ Â";
        case TypingAction::InsertDStroke: return L"Chữ đ";
        case TypingAction::InsertDStrokeUpper: return L"Chữ Đ";
        case TypingAction::InsertECircumflex: return L"Chữ ê";
        case TypingAction::InsertECircumflexUpper: return L"Chữ Ê";
        case TypingAction::InsertOCircumflex: return L"Chữ ô";
        case TypingAction::InsertOCircumflexUpper: return L"Chữ Ô";
        case TypingAction::InsertOHorn: return L"Chữ ơ";
        case TypingAction::InsertOHornUpper: return L"Chữ Ơ";
        case TypingAction::InsertUHorn: return L"Chữ ư";
        case TypingAction::InsertUHornUpper: return L"Chữ Ư";
    }
    return L"";
}

// Find the TypingAction whose Vietnamese label matches the given text.
// Returns None if not found. Used to map combo/listview text → enum.
[[nodiscard]] TypingAction ActionFromLabel(const wchar_t* label) noexcept {
    if (!label || !*label) return TypingAction::None;
    for (auto a = static_cast<uint8_t>(TypingAction::ClearTone);
         a <= static_cast<uint8_t>(TypingAction::InsertUHornUpper); ++a) {
        auto action = static_cast<TypingAction>(a);
        if (wcscmp(label, ActionLabel(action)) == 0) return action;
    }
    return TypingAction::None;
}
} // anonymous namespace

// ════════════════════════════════════════════════════════════
// Public
// ════════════════════════════════════════════════════════════

bool ClassicUserDefinedDialog::Show(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
    ClassicUserDefinedDialog dlg;
    if (!dlg.Init(hInstance, parent, forceLightTheme)) return false;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg.hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(dlg.hwnd_)) break;
    }
    return dlg.modified_;
}

// ════════════════════════════════════════════════════════════
// Init
// ════════════════════════════════════════════════════════════

bool ClassicUserDefinedDialog::Init(HINSTANCE hInstance, HWND parent, bool forceLightTheme) {
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
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Kiểu gõ Tự định nghĩa",
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

    CreateControls();
    LoadData();
    PopulateList();
    OnActionChanged();  // populate key field for default-selected action AFTER LoadData

    EnumChildWindows(hwnd_, [](HWND h, LPARAM lp) -> BOOL {
        auto* self = reinterpret_cast<ClassicUserDefinedDialog*>(lp);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(self->theme_.Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
    theme_.ThemeAllChildren(hwnd_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void ClassicUserDefinedDialog::CreateControls() {
    int x = Dpi(kPadding), y = Dpi(kPadding);
    int cw = Dpi(kWidth - kPadding * 2);
    int btnH = Dpi(kBtnHeight);
    int gap = Dpi(kBtnGap);
    int editH = theme_.ModernHeight();

    // Action-first layout: combo (action) on top, edit (key) below it,
    // then Apply / Clear buttons. ListView at the bottom shows current map.
    comboAction_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, cw, Dpi(300), hwnd_, reinterpret_cast<HMENU>(IDC_COMBO_ACTION), hInstance_, nullptr);

    // Iterate enum range [ClearTone .. InsertUHornUpper] — bounded by sentinels in
    // TypingAction.h, so adding new actions doesn't require touching this code.
    for (auto a = static_cast<uint8_t>(TypingAction::ClearTone);
         a <= static_cast<uint8_t>(TypingAction::InsertUHornUpper); ++a) {
        SendMessageW(comboAction_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ActionLabel(static_cast<TypingAction>(a))));
    }
    SendMessageW(comboAction_, CB_SETCURSEL, 0, 0);
    y += editH + gap;

    // Key edit + Apply + Clear buttons on one row
    int btnW = Dpi(80);
    int keyW = cw - btnW * 2 - gap * 2;
    editKey_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, keyW, editH, hwnd_, reinterpret_cast<HMENU>(IDC_EDIT_KEY), hInstance_, nullptr);
    SendMessageW(editKey_, EM_SETLIMITTEXT, 1, 0);
    theme_.ApplyModernEntryStyle(editKey_);

    btnApply_ = CreateWindowExW(0, L"BUTTON", L"Áp dụng",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + keyW + gap, y, btnW, editH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_APPLY), hInstance_, nullptr);
    btnClear_ = CreateWindowExW(0, L"BUTTON", L"Xoá phím",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + keyW + btnW + gap * 2, y, btnW, editH,
        hwnd_, reinterpret_cast<HMENU>(IDC_BTN_CLEAR), hInstance_, nullptr);
    y += editH + gap * 2;

    // Template row
    int templW = (cw - gap) / 2;
    btnLoadTelex_ = CreateWindowExW(0, L"BUTTON", L"Nạp mẫu Telex",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, templW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_LOAD_TELEX), hInstance_, nullptr);
    btnLoadVni_ = CreateWindowExW(0, L"BUTTON", L"Nạp mẫu VNI",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + templW + gap, y, templW, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_LOAD_VNI), hInstance_, nullptr);
    y += btnH + gap * 2;

    // ListView — 2 columns: Key, Action. Bottom of the dialog so the action
    // picker stays at the top where the eye lands first.
    int listH = Dpi(180);
    listView_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
        x, y, cw, listH, hwnd_, reinterpret_cast<HMENU>(IDC_LIST_KEYMAP), hInstance_, nullptr);
    ListView_SetExtendedListViewStyle(listView_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"Phím");
    col.cx = Dpi(60);
    ListView_InsertColumn(listView_, 0, &col);

    col.pszText = const_cast<wchar_t*>(L"Hành động");
    col.cx = cw - Dpi(60 + 24);
    ListView_InsertColumn(listView_, 1, &col);
    y += listH + gap;

    // Import / Export
    int abw = (cw - gap) / 2;
    btnImport_ = CreateWindowExW(0, L"BUTTON", L"Nhập từ file...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, abw, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_IMPORT), hInstance_, nullptr);
    btnExport_ = CreateWindowExW(0, L"BUTTON", L"Xuất ra file...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x + abw + gap, y, abw, btnH, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_EXPORT), hInstance_, nullptr);
}

void ClassicUserDefinedDialog::LoadData() {
    auto config = ConfigManager::LoadOrDefault();
    keyMap_ = config.customKeyMap;
}

void ClassicUserDefinedDialog::SaveData() {
    auto config = ConfigManager::LoadOrDefault();
    config.customKeyMap = keyMap_;
    (void)ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), config);
    SignalConfigChange();
    modified_ = true;
}

void ClassicUserDefinedDialog::PopulateList() {
    ListView_DeleteAllItems(listView_);

    struct Entry { uint8_t key; TypingAction action; };
    std::vector<Entry> entries;
    for (size_t i = 0; i < 128; ++i) {
        if (keyMap_[i] != TypingAction::None) {
            entries.push_back({ static_cast<uint8_t>(i), keyMap_[i] });
        }
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);

        wchar_t keyStr[2] = { static_cast<wchar_t>(entries[i].key), 0 };
        item.pszText = keyStr;
        ListView_InsertItem(listView_, &item);

        ListView_SetItemText(listView_, static_cast<int>(i), 1,
                             const_cast<wchar_t*>(ActionLabel(entries[i].action)));
    }
}

void ClassicUserDefinedDialog::ApplyAction() {
    wchar_t actionBuf[128] = {0};
    GetWindowTextW(comboAction_, actionBuf, 128);
    TypingAction action = ActionFromLabel(actionBuf);
    if (action == TypingAction::None) return;

    wchar_t key[2] = {0};
    GetWindowTextW(editKey_, key, 2);
    if (!key[0]) return;

    wchar_t k = towlower(key[0]);
    if (k >= 128) return;

    ApplyActionToMap(action, k);
    SaveData();
    PopulateList();
}

void ClassicUserDefinedDialog::ClearSelectedAction() {
    wchar_t actionBuf[128] = {0};
    GetWindowTextW(comboAction_, actionBuf, 128);
    TypingAction action = ActionFromLabel(actionBuf);
    if (action == TypingAction::None) return;

    ClearActionFromMap(action);
    SaveData();
    PopulateList();
    SetWindowTextW(editKey_, L"");
}

void ClassicUserDefinedDialog::OnActionChanged() {
    wchar_t actionBuf[128] = {0};
    GetWindowTextW(comboAction_, actionBuf, 128);
    TypingAction action = ActionFromLabel(actionBuf);

    // Find currently-assigned key for this action (if any).
    wchar_t buf[2] = {0};
    if (action != TypingAction::None) {
        for (size_t i = 0; i < 128; ++i) {
            if (keyMap_[i] == action) {
                buf[0] = static_cast<wchar_t>(i);
                break;
            }
        }
    }
    SetWindowTextW(editKey_, buf);
}

void ClassicUserDefinedDialog::OnListSelectionChanged() {
    int sel = ListView_GetNextItem(listView_, -1, LVNI_SELECTED);
    if (sel == -1) return;

    wchar_t key[2] = {0};
    ListView_GetItemText(listView_, sel, 0, key, 2);

    wchar_t actionBuf[128] = {0};
    ListView_GetItemText(listView_, sel, 1, actionBuf, 128);

    // Map listview label → enum → combo index. Direct mapping is safer than
    // CB_FINDSTRINGEXACT which is case-INSENSITIVE: "Chữ ă" and "Chữ Ă" would
    // collide. Combo is populated in enum order [ClearTone..InsertUHornUpper]
    // so combo index = (enum value) - (ClearTone enum value).
    TypingAction action = ActionFromLabel(actionBuf);
    if (action != TypingAction::None) {
        int idx = static_cast<int>(action) - static_cast<int>(TypingAction::ClearTone);
        SendMessageW(comboAction_, CB_SETCURSEL, idx, 0);
    }
    SetWindowTextW(editKey_, key);
}

void ClassicUserDefinedDialog::ApplyActionToMap(TypingAction action, wchar_t newKey) noexcept {
    if (action == TypingAction::None) return;
    for (auto& slot : keyMap_) {
        if (slot == action) slot = TypingAction::None;
    }
    if (newKey < 128) {
        keyMap_[static_cast<uint8_t>(newKey)] = action;
    }
}

void ClassicUserDefinedDialog::ClearActionFromMap(TypingAction action) noexcept {
    if (action == TypingAction::None) return;
    for (auto& slot : keyMap_) {
        if (slot == action) slot = TypingAction::None;
    }
}

void ClassicUserDefinedDialog::LoadTemplate(bool telex) {
    keyMap_.fill(TypingAction::None);
    std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789[]";
    for (char c : chars) {
        TypingAction action = ClassifyKey(static_cast<wchar_t>(c), telex, !telex);
        if (action != TypingAction::None) {
            keyMap_[static_cast<uint8_t>(c)] = action;
        }
    }
    SaveData();
    PopulateList();
}

void ClassicUserDefinedDialog::ImportFromFile() {
    std::wstring path = OpenFileDialog(hwnd_, L"Keymap files (*.keymap)\0*.keymap\0All files (*.*)\0*.*\0", L"Chọn file keymap");
    if (path.empty()) return;

    TypingConfig imported;
    if (ConfigManager::ImportCustomKeyMap(path, imported)) {
        keyMap_ = imported.customKeyMap;
        SaveData();
        PopulateList();
    }
}

void ClassicUserDefinedDialog::ExportToFile() {
    std::wstring path = SaveFileDialog(hwnd_, L"Keymap files (*.keymap)\0*.keymap\0", L"Lưu file keymap", L"keymap");
    if (path.empty()) return;

    TypingConfig out;
    out.customKeyMap = keyMap_;
    (void)ConfigManager::ExportCustomKeyMap(path, out);
}

LRESULT CALLBACK ClassicUserDefinedDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ClassicUserDefinedDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicUserDefinedDialog*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND: {
            uint16_t id = LOWORD(wParam);
            uint16_t code = HIWORD(wParam);
            if (id == IDC_BTN_APPLY) self->ApplyAction();
            else if (id == IDC_BTN_CLEAR) self->ClearSelectedAction();
            else if (id == IDC_BTN_LOAD_TELEX) self->LoadTemplate(true);
            else if (id == IDC_BTN_LOAD_VNI) self->LoadTemplate(false);
            else if (id == IDC_BTN_IMPORT) self->ImportFromFile();
            else if (id == IDC_BTN_EXPORT) self->ExportToFile();
            else if (id == IDC_COMBO_ACTION && code == CBN_SELCHANGE) self->OnActionChanged();
            break;
        }
        case WM_NOTIFY: {
            auto* nmhdr = reinterpret_cast<LPNMHDR>(lParam);
            if (nmhdr && nmhdr->idFrom == IDC_LIST_KEYMAP && nmhdr->code == LVN_ITEMCHANGED) {
                auto* nmlv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                if (nmlv && (nmlv->uChanged & LVIF_STATE) && (nmlv->uNewState & LVIS_SELECTED)) {
                    self->OnListSelectionChanged();
                }
            }
            break;
        }
        case WM_ERASEBKGND:
        case WM_PRINTCLIENT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, self->theme_.BrushBackground());
            return 1;
        }
        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(
                self->theme_.OnCtlColorStatic(
                    reinterpret_cast<HDC>(wParam),
                    reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(
                self->theme_.OnCtlColorBtn(
                    reinterpret_cast<HDC>(wParam),
                    reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLOREDIT:
            return reinterpret_cast<LRESULT>(
                self->theme_.OnCtlColorEdit(
                    reinterpret_cast<HDC>(wParam),
                    reinterpret_cast<HWND>(lParam)));
        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(
                self->theme_.OnCtlColorListBox(
                    reinterpret_cast<HDC>(wParam),
                    reinterpret_cast<HWND>(lParam)));

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            // Do NOT PostQuitMessage here. Show()'s pump exits via the
            // IsWindow(dlg.hwnd_) check after WM_DESTROY runs synchronously
            // inside DispatchMessage. Posting WM_QUIT would shortcircuit the
            // pump but leave WM_QUIT in the thread queue for the parent
            // settings dialog's pump to consume — closing the parent too.
            self->hwnd_ = nullptr;
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ClassicUserDefinedDialog::Dpi(int value) const noexcept {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

} // namespace NextKey::Classic
