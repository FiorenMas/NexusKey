// NexusKey Classic — Settings Dialog Implementation
// Compact (Unikey-style) + Advanced (EVKey-style) modes
// SPDX-License-Identifier: GPL-3.0-only

#include "ClassicSettingsDialog.h"
#include "ClassicExcludedAppsDialog.h"
#include "ClassicSpellExclusionsDialog.h"
#include "ClassicAppOverridesDialog.h"
#include "ClassicMacroTableDialog.h"
#include "ClassicConvertToolDialog.h"
#include "ClassicIconColorDialog.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/SharedConstants.h"
#include "core/Debug.h"
#include "core/Version.h"
#include "system/StartupHelper.h"
#include "system/UpdateChecker.h"
#include "system/PendingDllApply.h"
#include "core/Strings.h"

#include <thread>
#include <atomic>
#include <memory>

#include <windowsx.h>

namespace NextKey::Classic {

// Private WM for update button state changes from background thread
// wParam: 0=restore, 1=checking, 2=downloading
constexpr UINT WM_UPDATE_BTN_STATE = WM_APP;

// ════════════════════════════════════════════════════════════════════
// Lifecycle
// ════════════════════════════════════════════════════════════════════

ClassicSettingsDialog::~ClassicSettingsDialog() {
    for (auto& icon : tabIcons_) {
        if (icon) { DestroyIcon(icon); icon = nullptr; }
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool ClassicSettingsDialog::Show(HINSTANCE hInstance, HWND parent) {
    hInstance_ = hInstance;

    if (!RegisterWindowClass(hInstance))
        return false;

    LoadSettings();

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(
        0,
        kClassName,
        L"NexusKey v" NEXUSKEY_VERSION_WSTR,
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,  // temporary size
        parent, nullptr, hInstance, this
    );

    if (!hwnd_)
        return false;

    // Get real DPI from the window's monitor
    dpi_ = Classic::GetWindowDpi(hwnd_);

    // Resize to correct DPI-scaled full size and center
    int width  = Dpi(kAdvancedWidth);
    int height = Dpi(kAdvancedHeight);
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, style, FALSE);
    int adjWidth  = rc.right - rc.left;
    int adjHeight = rc.bottom - rc.top;
    int x = (screenW - adjWidth) / 2;
    int y = (screenH - adjHeight) / 2;
    SetWindowPos(hwnd_, nullptr, x, y, adjWidth, adjHeight, SWP_NOZORDER | SWP_NOACTIVATE);

    theme_.Init(hwnd_, systemConfig_.forceLightTheme);
    theme_.ApplyWindowAttributes(hwnd_);

    fontSmall_ = CreateFontW(Dpi(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    CreateCompactControls();
    CreateAdvancedControls();
    SetupTooltips();
    PopulateControls();
    SetFontOnAllChildren();
    theme_.ThemeAllChildren(hwnd_);

    // Apply English labels if language was already set to English
    if (GetLanguage() == Language::English)
        RefreshLabels();

    if (tabControl_) ShowWindow(tabControl_, SW_SHOW);
    ShowTabPage(currentTab_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // Classic uses a one-shot confirm instead of an inline banner (Win32 tab
    // layout makes an inline strip costly). User clicks OK → ExitWindowsEx;
    // Cancel → continue with settings.
    if (auto bannerState = GetUpdateBannerState(sharedState_);
        bannerState != UpdateBannerState::Hide) {
        const wchar_t* msg = (bannerState == UpdateBannerState::PendingSwap)
            ? S(StringId::UPDATE_BANNER_PENDING)
            : S(StringId::UPDATE_BANNER_MISMATCH);
        // User already confirms reboot intent in THIS MessageBox; skip the
        // second prompt in RestartWindowsWithPrompt — call the no-UI variant.
        if (MessageBoxW(hwnd_, msg, L"NexusKey",
                        MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK) {
            RestartWindowsNow();
        }
    }

    // Modal message loop
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return true;
}

// ════════════════════════════════════════════════════════════════════
// Window class registration
// ════════════════════════════════════════════════════════════════════

bool ClassicSettingsDialog::RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = sizeof(void*);
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // We handle WM_ERASEBKGND
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = kClassName;
    wc.hIconSm      = wc.hIcon;

    ATOM atom = RegisterClassExW(&wc);
    return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// ════════════════════════════════════════════════════════════════════
// Control creation — Compact mode
// ════════════════════════════════════════════════════════════════════

static LRESULT CALLBACK HotkeyEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
    if (msg == WM_CHAR) {
        wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch == VK_BACK) {
            SetWindowTextW(hWnd, L"");
            return 0;
        }
        if (ch == L' ') {
            SetWindowTextW(hWnd, L"Space");
            SendMessageW(hWnd, EM_SETSEL, (WPARAM)-1, 0);
            return 0;
        }
        if (ch >= 32) {
            if (ch >= L'a' && ch <= L'z') ch = ch - L'a' + L'A';
            wchar_t buf[2] = { ch, 0 };
            SetWindowTextW(hWnd, buf);
            SendMessageW(hWnd, EM_SETSEL, (WPARAM)-1, 0);
            return 0;
        }
    } else if (msg == WM_KEYDOWN) {
        if (wParam == VK_DELETE) {
            SetWindowTextW(hWnd, L"");
            return 0;
        }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

void ClassicSettingsDialog::CreateCompactControls() {
    int offset = Dpi(8);
    int gbX = Dpi(8);

    CreateLabel(L"  Cơ bản", gbX + Dpi(6), Dpi(2) + offset, Dpi(90), Dpi(18), 2999);

    int x1 = Dpi(kPadding);
    int y = Dpi(kPadding + 8) + offset;
    int contentW = Dpi(kAdvancedWidth - kPadding * 2);

    int colW = (contentW - Dpi(kPadding)) / 2;
    int col2X = x1 + colW + Dpi(kPadding);

    // Row 1: "Kieu go" and "Bang ma" side by side
    CreateLabel(L"Kiểu gõ", x1, y, colW, Dpi(kLabelHeight), IDC_STATIC_METHOD);
    CreateLabel(L"Bảng mã", col2X, y, colW, Dpi(kLabelHeight), IDC_STATIC_ENCODING);
    y += Dpi(kLabelHeight + kRowGap);

    comboMethod_ = CreateCombo(x1, y, colW, Dpi(kComboHeight + 120), IDC_COMBO_METHOD);
    ComboBox_AddString(comboMethod_, L"Telex");
    ComboBox_AddString(comboMethod_, L"VNI");
    ComboBox_AddString(comboMethod_, L"Simple Telex");
    ComboBox_AddString(comboMethod_, L"Telex + VNI");

    comboEncoding_ = CreateCombo(col2X, y, colW, Dpi(kComboHeight + 120), IDC_COMBO_ENCODING);
    ComboBox_AddString(comboEncoding_, L"Unicode");
    ComboBox_AddString(comboEncoding_, L"TCVN3 (ABC)");
    ComboBox_AddString(comboEncoding_, L"VNI Windows");
    ComboBox_AddString(comboEncoding_, L"Unicode tổ hợp");
    ComboBox_AddString(comboEncoding_, L"Việt (CP 1258)");

    y += Dpi(kComboHeight + kSectionGap);

    // Row 2: "Phim tat"
    CreateLabel(L"Phím chuyển (Việt/Anh)", x1, y, contentW, Dpi(kLabelHeight), IDC_STATIC_SWITCHKEY);
    y += Dpi(kLabelHeight + kRowGap);

    int btnW = Dpi(55);
    int cx = x1;
    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        if (kSettings[i].owner == SettingOwner::Hotkey) {
            checkControls_[i] = CreateCheck(kSettings[i].label, cx, y, btnW, Dpi(kControlHeight), kSettings[i].win32Id);
            cx += btnW + Dpi(4);
        }
    }
    
    int editH = Dpi(16);
    int editYOffset = (Dpi(kControlHeight) - editH) / 2;
    int editW = Dpi(46);
    editHotkey_ = CreateEdit(cx + Dpi(2), y + editYOffset, editW, editH, IDC_EDIT_SWITCH_KEY);
    SetWindowLongW(editHotkey_, GWL_STYLE, GetWindowLongW(editHotkey_, GWL_STYLE) | ES_CENTER);
    SendMessageW(editHotkey_, EM_SETLIMITTEXT, 5, 0);
    SetWindowSubclass(editHotkey_, HotkeyEditSubclassProc, 1, 0);

    // Render "Tiếng bíp khi chuyển" right after the hotkey edit textbox
    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        if (wcscmp(kSettings[i].id, L"beep-sound") == 0) {
            checkControls_[i] = CreateCheck(kSettings[i].label, cx + editW + Dpi(18), y, Dpi(130), Dpi(kControlHeight), kSettings[i].win32Id);
            break;
        }
    }

    y += Dpi(kControlHeight + kSectionGap) + Dpi(4);
}

// ════════════════════════════════════════════════════════════════════
// Control creation — Advanced mode (tab + checkboxes)
// ════════════════════════════════════════════════════════════════════

LRESULT CALLBACK TabSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    auto* self = reinterpret_cast<ClassicSettingsDialog*>(dwRefData);

    if (uMsg == WM_ERASEBKGND || uMsg == WM_PRINTCLIENT) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, self->theme().BrushBackground());
        return 1;
    }

    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        FillRect(hdc, &rcClient, self->theme().BrushBackground());

        int activeTab = TabCtrl_GetCurSel(hWnd);
        int tabCount = TabCtrl_GetItemCount(hWnd);

        HPEN pen = CreatePen(PS_SOLID, 1, self->theme().Colors().border);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);

        HFONT fontActive = self->theme().Fonts().bodyBold;
        HFONT fontInactive = self->theme().Fonts().body;
        SetBkMode(hdc, TRANSPARENT);

        RECT activeRc = {0};
        int topY = 0;
        if (tabCount > 0) {
            TabCtrl_GetItemRect(hWnd, 0, &activeRc);
            activeRc.left = 0;
            topY = activeRc.bottom;
            if (activeTab != -1) {
                TabCtrl_GetItemRect(hWnd, activeTab, &activeRc);
                if (activeTab == 0) activeRc.left = 0;
            }
        }

        int r = Classic::DpiScale(12, Classic::GetWindowDpi(hWnd));

        RoundRect(hdc, rcClient.left, topY, rcClient.right, rcClient.bottom, r, r);

        if (activeTab == 0) {
            int half = r / 2;
            RECT cornerRc = { rcClient.left, topY, rcClient.left + half, topY + half };
            FillRect(hdc, &cornerRc, self->theme().BrushBackground());
            
            MoveToEx(hdc, rcClient.left, topY, nullptr);
            LineTo(hdc, rcClient.left, topY + half + 1);
        }

        if (activeTab != -1) {
            HPEN eraPen = CreatePen(PS_SOLID, 1, self->theme().Colors().background);
            HPEN eraOld = (HPEN)SelectObject(hdc, eraPen);
            MoveToEx(hdc, activeRc.left + 1, topY, nullptr);
            LineTo(hdc, activeRc.right, topY);
            SelectObject(hdc, eraOld);
            DeleteObject(eraPen);
        }

        for (int i = 0; i < tabCount; ++i) {
            RECT rcTab;
            TabCtrl_GetItemRect(hWnd, i, &rcTab);
            if (i == 0) rcTab.left = 0;
            
            bool isSelected = (i == activeTab);
            
            HRGN hRgn = CreateRectRgn(rcTab.left, rcTab.top, rcTab.right, rcTab.bottom + (isSelected ? 1 : 0));
            SelectClipRgn(hdc, hRgn);

            RoundRect(hdc, rcTab.left, rcTab.top, rcTab.right, rcTab.bottom + r, r, r);

            SelectClipRgn(hdc, NULL);
            DeleteObject(hRgn);

            wchar_t text[64]{};
            TCITEMW tci = { TCIF_TEXT, 0, 0, text, 64 };
            SendMessageW(hWnd, TCM_GETITEMW, i, reinterpret_cast<LPARAM>(&tci));

            SetTextColor(hdc, isSelected ? self->theme().Colors().text : self->theme().Colors().textSecondary);
            SelectObject(hdc, isSelected ? fontActive : fontInactive);

            HICON icon = (i < 3) ? self->tabIcons_[i] : nullptr;
            if (icon) {
                int iconSz = self->iconSize_;
                int gap = Classic::DpiScale(4, Classic::GetWindowDpi(hWnd));
                SIZE textSz{};
                GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &textSz);
                int totalW = iconSz + gap + textSz.cx;
                int startX = rcTab.left + (rcTab.right - rcTab.left - totalW) / 2;
                int iconY = rcTab.top + (rcTab.bottom - rcTab.top - iconSz) / 2;

                DrawIconEx(hdc, startX, iconY, icon, iconSz, iconSz, 0, nullptr, DI_NORMAL);

                RECT rcText = rcTab;
                rcText.left = startX + iconSz + gap;
                DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else {
                DrawTextW(hdc, text, -1, &rcTab, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        EndPaint(hWnd, &ps);
        return 0;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void ClassicSettingsDialog::CreateAdvancedControls() {
    if (advancedCreated_) return;
    advancedCreated_ = true;

    int x = Dpi(8);
    // Tab control starts below compact section
    RECT editRc;
    GetWindowRect(editHotkey_, &editRc);
    MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<LPPOINT>(&editRc), 2);
    int compactBottom = editRc.bottom + Dpi(kSectionGap + 12);

    int tabW = Dpi(kAdvancedWidth - 16);
    int tabH = Dpi(kAdvancedHeight - 8) - compactBottom;

    tabControl_ = CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH,
        x, compactBottom, tabW, tabH,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB_ADVANCED)),
        hInstance_, nullptr
    );

    SetWindowSubclass(tabControl_, TabSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // Load tab icons (color .ico replacing monochrome emoji)
    iconSize_ = Dpi(16);
    const int iconIds[] = { IDI_TAB_INPUT, IDI_TAB_MACROS, IDI_TAB_SYSTEM };
    for (int i = 0; i < 3; ++i) {
        tabIcons_[i] = static_cast<HICON>(LoadImageW(
            hInstance_, MAKEINTRESOURCEW(iconIds[i]), IMAGE_ICON,
            iconSize_, iconSize_, LR_DEFAULTCOLOR));
    }

    // Insert tabs (text only — icons drawn by TabSubclassProc)
    TCITEMW tie{};
    tie.mask = TCIF_TEXT;

    tie.pszText = const_cast<wchar_t*>(L"Bộ Gõ");
    TabCtrl_InsertItem(tabControl_, 0, &tie);

    tie.pszText = const_cast<wchar_t*>(L"Gõ Tắt");
    TabCtrl_InsertItem(tabControl_, 1, &tie);

    tie.pszText = const_cast<wchar_t*>(L"Hệ thống");
    TabCtrl_InsertItem(tabControl_, 2, &tie);

    TabCtrl_SetItemSize(tabControl_, Dpi(120), Dpi(kTabHeight));

    // Get tab content area
    RECT tabRect{};
    GetClientRect(tabControl_, &tabRect);
    TabCtrl_AdjustRect(tabControl_, FALSE, &tabRect);

    tabRect.left += Dpi(12);
    tabRect.right -= Dpi(12);
    tabRect.top += Dpi(8);

    // Map tab content rect to parent coordinates
    POINT tabOrigin = { tabRect.left, tabRect.top };
    ClientToScreen(tabControl_, &tabOrigin);
    ScreenToClient(hwnd_, &tabOrigin);

    int contentLeft = tabOrigin.x;
    int contentTop  = tabOrigin.y;
    int colWidth    = (tabRect.right - tabRect.left - Dpi(8)) / 2;

    // Create checkboxes from SettingMetadata
    int rowCounts[3][2] = {};  // [tab][column]

    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.win32Id == 0 || meta.owner == SettingOwner::Hotkey || wcscmp(meta.id, L"beep-sound") == 0)
            continue;

        int tab = meta.tab;
        int col = meta.column;
        int row = rowCounts[tab][col]++;

        // If it's an inline action button
        bool isInlineAction = (meta.type == SettingType::Action && 
            (wcscmp(meta.label, L"...") == 0 || meta.win32Id == IDC_BTN_CHECK_UPDATE));
        if (isInlineAction) {
            rowCounts[tab][col]--; // stay on the same visual row
            row--; // go back to the row we just incremented past
        }

        int cx = contentLeft + col * (colWidth + Dpi(8));
        int cy = contentTop + row * Dpi(kControlHeight + kRowGap);

        if (meta.type == SettingType::Toggle) {
            bool hasInlineNext = ((i + 1 < kSettingsCount) && kSettings[i+1].type == SettingType::Action && 
                (wcscmp(kSettings[i+1].label, L"...") == 0 || kSettings[i+1].win32Id == IDC_BTN_CHECK_UPDATE));
            int nextBtnW = 0;
            if (hasInlineNext) {
                nextBtnW = (wcscmp(kSettings[i+1].label, L"...") == 0) ? Dpi(26) : Dpi(70);
            }
            int checkW = hasInlineNext ? colWidth - nextBtnW - Dpi(4) : colWidth;
            checkControls_[i] = CreateCheck(meta.label, cx, cy, checkW, Dpi(kControlHeight), meta.win32Id);
        } else if (meta.type == SettingType::Action) {
            if (isInlineAction) {
                int btnW = (wcscmp(meta.label, L"...") == 0) ? Dpi(26) : Dpi(70);
                int inlineCx = cx + colWidth - btnW;
                checkControls_[i] = CreateBtn(meta.label, inlineCx, cy, btnW, Dpi(kControlHeight), meta.win32Id);
            } else {
                checkControls_[i] = CreateBtn(meta.label, cx, cy, colWidth, Dpi(kControlHeight), meta.win32Id);
            }
        } else if (meta.type == SettingType::Dropdown) {
            int lblW = Dpi(100);
            int comboW = colWidth - lblW - Dpi(4);

            HWND lbl = CreateLabel(meta.label, cx, cy + Dpi(4), lblW, Dpi(kControlHeight), 0);
            extraControls_[i] = lbl;
            HWND combo = CreateCombo(cx + lblW + Dpi(4), cy, comboW, Dpi(kComboHeight + 60), meta.win32Id);
            if (wcscmp(meta.id, L"custom-icon-style") == 0) {
                ComboBox_AddString(combo, L"Màu mặc định");
                ComboBox_AddString(combo, L"Nền tối");
                ComboBox_AddString(combo, L"Nền sáng");
                ComboBox_AddString(combo, L"Tự chọn");
            }
            if (wcscmp(meta.id, L"startup-mode") == 0) {
                ComboBox_AddString(combo, L"Tiếng Việt");
                ComboBox_AddString(combo, L"Tiếng Anh");
                ComboBox_AddString(combo, L"Ghi nhớ");
            }
            checkControls_[i] = combo;
        }
        
        if (checkControls_[i]) {
            ShowWindow(checkControls_[i], SW_HIDE);
        }
        if (extraControls_[i]) {
            ShowWindow(extraControls_[i], SW_HIDE);
        }
    }

    // "Báo cáo lỗi" link below tab control
    {
        RECT tcRc;
        GetWindowRect(tabControl_, &tcRc);
        MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<LPPOINT>(&tcRc), 2);
        linkReportBug_ = CreateWindowExW(0, WC_LINK,
            L"<a href=\"https://github.com/phatMT97/NexusKey/issues\">Báo cáo lỗi</a>",
            WS_CHILD | WS_VISIBLE,
            tcRc.right - Dpi(90), tcRc.bottom - Dpi(26), Dpi(80), Dpi(16),
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LINK_REPORT_BUG)),
            hInstance_, nullptr);
    }

    // Apply font to newly created controls
    SetFontOnAllChildren();

    // Show first tab
    ShowTabPage(0);
}



// ════════════════════════════════════════════════════════════════════
// Settings I/O
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::LoadSettings() {
    config_       = ConfigManager::LoadOrDefault();
    hotkeyConfig_ = ConfigManager::LoadHotkeyConfigOrDefault();
    systemConfig_ = ConfigManager::LoadSystemConfigOrDefault();

    (void)sharedState_.OpenReadWrite();
    (void)configEvent_.Initialize();
}

void ClassicSettingsDialog::PopulateControls() {
    if (comboMethod_)
        ComboBox_SetCurSel(comboMethod_, static_cast<int>(config_.inputMethod));
    if (comboEncoding_)
        ComboBox_SetCurSel(comboEncoding_, static_cast<int>(config_.codeTable));

    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.win32Id == 0)
            continue;

        HWND ctrl = checkControls_[i];
        if (!ctrl) continue;

        if (meta.type == SettingType::Toggle) {
            bool value = false;
            switch (meta.owner) {
                case SettingOwner::Typing:
                    value = *reinterpret_cast<const bool*>(
                        reinterpret_cast<const char*>(&config_) + meta.offset);
                    break;
                case SettingOwner::Hotkey:
                    value = *reinterpret_cast<const bool*>(
                        reinterpret_cast<const char*>(&hotkeyConfig_) + meta.offset);
                    break;
                case SettingOwner::System:
                    value = *reinterpret_cast<const bool*>(
                        reinterpret_cast<const char*>(&systemConfig_) + meta.offset);
                    break;
                default:
                    break;
            }
            CheckDlgButton(hwnd_, meta.win32Id, value ? BST_CHECKED : BST_UNCHECKED);
        } else if (meta.type == SettingType::Dropdown) {
            uint8_t value = 0;
            switch (meta.owner) {
                case SettingOwner::System:
                    value = *reinterpret_cast<const uint8_t*>(
                        reinterpret_cast<const char*>(&systemConfig_) + meta.offset);
                    break;
                default:
                    break;
            }
            ComboBox_SetCurSel(ctrl, static_cast<int>(value));
        }
    }

    if (editHotkey_) {
        if (hotkeyConfig_.key == L' ') {
            SetWindowTextW(editHotkey_, L"Space");
        } else if (hotkeyConfig_.key) {
            wchar_t buf[2] = { hotkeyConfig_.key, 0 };
            SetWindowTextW(editHotkey_, buf);
        } else {
            SetWindowTextW(editHotkey_, L"");
        }
    }

    UpdateSpellCheckChildren();
}

void ClassicSettingsDialog::ReadControlValues() {
    if (comboMethod_) {
        int sel = ComboBox_GetCurSel(comboMethod_);
        if (sel >= 0 && sel <= 3)
            config_.inputMethod = static_cast<InputMethod>(sel);
    }
    if (comboEncoding_) {
        int sel = ComboBox_GetCurSel(comboEncoding_);
        if (sel >= 0 && sel <= 4)
            config_.codeTable = static_cast<CodeTable>(sel);
    }

    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        if (meta.win32Id == 0)
            continue;

        HWND ctrl = checkControls_[i];
        if (!ctrl) continue;

        if (meta.type == SettingType::Toggle) {
            bool checked = (IsDlgButtonChecked(hwnd_, meta.win32Id) == BST_CHECKED);
            switch (meta.owner) {
                case SettingOwner::Typing:
                    *reinterpret_cast<bool*>(
                        reinterpret_cast<char*>(&config_) + meta.offset) = checked;
                    break;
                case SettingOwner::Hotkey:
                    *reinterpret_cast<bool*>(
                        reinterpret_cast<char*>(&hotkeyConfig_) + meta.offset) = checked;
                    break;
                case SettingOwner::System:
                    *reinterpret_cast<bool*>(
                        reinterpret_cast<char*>(&systemConfig_) + meta.offset) = checked;
                    break;
                default:
                    break;
            }
        } else if (meta.type == SettingType::Dropdown) {
            int sel = ComboBox_GetCurSel(ctrl);
            if (sel >= 0) {
                uint8_t value = static_cast<uint8_t>(sel);
                switch (meta.owner) {
                    case SettingOwner::System:
                        *reinterpret_cast<uint8_t*>(
                            reinterpret_cast<char*>(&systemConfig_) + meta.offset) = value;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (editHotkey_) {
        wchar_t buf[16] = {0};
        GetWindowTextW(editHotkey_, buf, 16);
        if (wcscmp(buf, L"Space") == 0) {
            hotkeyConfig_.key = L' ';
        } else if (wcslen(buf) > 0) {
            wchar_t key = buf[0];
            if (key >= L'a' && key <= L'z') key = key - L'a' + L'A';
            hotkeyConfig_.key = key;
        } else {
            hotkeyConfig_.key = 0;
        }
    }

}

void ClassicSettingsDialog::SaveSettings() {
    ReadControlValues();
    SyncToSharedState();

    configDirty_ = true;
    KillTimer(hwnd_, kTimerDeferredSave);
    SetTimer(hwnd_, kTimerDeferredSave, kDeferredSaveDelayMs, nullptr);
}

void ClassicSettingsDialog::SyncToSharedState() {
    if (sharedState_.IsConnected()) {
        SharedState state = sharedState_.Read();
        if (state.IsValid()) {
            state.inputMethod = static_cast<uint8_t>(config_.inputMethod);
            state.spellCheck = config_.spellCheckEnabled ? 1 : 0;
            state.codeTable = static_cast<uint8_t>(config_.codeTable);
            state.SetFeatureFlags(EncodeFeatureFlags(config_));
            state.SetHotkey(hotkeyConfig_);
            state.configGeneration++;
            sharedState_.Write(state);
        }
    }

    if (configEvent_.IsValid()) {
        configEvent_.Signal();
    }
}

void ClassicSettingsDialog::SaveToToml() {
    std::wstring path = ConfigManager::GetConfigPath();
    if (!ConfigManager::SaveToFile(path, config_)) {
        NEXTKEY_LOG(L"[ClassicSettings] Failed to save typing config");
    }
    (void)ConfigManager::SaveHotkeyConfig(path, hotkeyConfig_);
    (void)ConfigManager::SaveSystemConfig(path, systemConfig_);

    configDirty_ = false;

    if (sharedState_.IsConnected()) {
        SharedState state = sharedState_.Read();
        if (state.IsValid()) {
            state.configGeneration++;
            sharedState_.Write(state);
        }
    }

    if (configEvent_.IsValid()) {
        configEvent_.Signal();
    }
}

// ════════════════════════════════════════════════════════════════════
// Tab management
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnTabChange() {
    if (!tabControl_) return;
    int tab = TabCtrl_GetCurSel(tabControl_);
    if (tab >= 0 && tab <= 2) {
        currentTab_ = tab;
        ShowTabPage(tab);
    }
}

void ClassicSettingsDialog::ShowTabPage(int tabIndex) {
    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        HWND ctrl = checkControls_[i];
        if (!ctrl) continue;

        const auto& meta = kSettings[i];
        if (meta.owner == SettingOwner::Hotkey || wcscmp(meta.id, L"beep-sound") == 0) continue;

        int showCmd = (meta.tab == tabIndex) ? SW_SHOW : SW_HIDE;
        ShowWindow(ctrl, showCmd);

        if (extraControls_[i]) {
            ShowWindow(extraControls_[i], showCmd);
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// Command handler
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnCommand(WPARAM wParam, LPARAM lParam) {
    UINT code = HIWORD(wParam);
    UINT id   = LOWORD(wParam);

    switch (id) {

        case IDC_BTN_CLOSE:
            DestroyWindow(hwnd_);
            return;

        case IDC_BTN_EXIT:
            if (configDirty_) {
                KillTimer(hwnd_, kTimerDeferredSave);
                SaveToToml();
            }
            DestroyWindow(hwnd_);
            PostQuitMessage(0);
            return;

        case IDC_COMBO_METHOD:
        case IDC_COMBO_ENCODING:
            if (code == CBN_SELCHANGE) {
                SaveSettings();
            } else if (code == CBN_DROPDOWN && theme_.IsDark()) {
                BOOL anim = FALSE;
                SystemParametersInfoW(SPI_GETCOMBOBOXANIMATION, 0, &anim, 0);
                if (anim) {
                    SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0, (PVOID)FALSE, 0);
                    SetPropW(reinterpret_cast<HWND>(lParam), L"WasAnim", reinterpret_cast<HANDLE>(1));
                }
            } else if (code == CBN_CLOSEUP) {
                if (GetPropW(reinterpret_cast<HWND>(lParam), L"WasAnim")) {
                    SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0, (PVOID)TRUE, 0);
                    RemovePropW(reinterpret_cast<HWND>(lParam), L"WasAnim");
                }
            }
            return;

        case IDC_BTN_SPELL_EXCLUSIONS:
            if (code == BN_CLICKED) {
                OnActionButton(static_cast<uint16_t>(id));
            }
            return;

        default:
            break;
    }

    // Check if this is a BN_CLICKED on a setting checkbox/button or CBN_SELCHANGE
    if (code == BN_CLICKED || code == CBN_SELCHANGE) {
        const auto* meta = FindSettingByControlId(static_cast<uint16_t>(id));
        if (meta) {
            if (meta->type == SettingType::Action) {
                OnActionButton(meta->win32Id);
            } else {
                // Icon style dropdown change
                if (meta->type == SettingType::Dropdown && wcscmp(meta->id, L"custom-icon-style") == 0
                    && code == CBN_SELCHANGE) {
                    int sel = ComboBox_GetCurSel(reinterpret_cast<HWND>(lParam));
                    if (sel == 3) { // "Tự chọn" → open color picker
                        OnPickIconColors();
                    }
                    // All icon style changes need immediate flush + notify
                    SaveSettings();
                    KillTimer(hwnd_, kTimerDeferredSave);
                    SaveToToml();
                    HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
                    if (trayWnd) PostMessageW(trayWnd, WM_NEXUSKEY_ICON_CHANGED, 0, 0);
                    return;
                }

                if (meta->type == SettingType::Dropdown && wcscmp(meta->id, L"startup-mode") == 0
                    && code == CBN_SELCHANGE) {
                    // Startup mode change — immediate save
                    SaveSettings();
                    KillTimer(hwnd_, kTimerDeferredSave);
                    SaveToToml();
                    return;
                }

                SaveSettings();

                // Spell check controls child toggles (zwjf, auto-restore, exclusions button)
                if (meta->win32Id == IDC_CHECK_SPELL) {
                    UpdateSpellCheckChildren();
                }

                // System toggles have side effects beyond config save
                if (meta->owner == SettingOwner::System && meta->type == SettingType::Toggle) {
                    bool checked = (IsDlgButtonChecked(hwnd_, meta->win32Id) == BST_CHECKED);
                    OnSystemToggle(meta->id, checked);
                }
            }
        }
    }

    // Check if this is an EN_CHANGE on edit control
    if (code == EN_CHANGE && id == IDC_EDIT_SWITCH_KEY) {
        SaveSettings();
    }
}

// ════════════════════════════════════════════════════════════════════
// Action button handlers
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnActionButton(uint16_t controlId) {
    switch (controlId) {
        case IDC_BTN_APP_OVERRIDES:
            ClassicAppOverridesDialog::Show(hInstance_, hwnd_, systemConfig_.forceLightTheme);
            // Reload config in case overrides changed
            LoadSettings();
            PopulateControls();
            break;

        case IDC_BTN_EXCLUDE_APPS:
            ClassicExcludedAppsDialog::Show(hInstance_, hwnd_, systemConfig_.forceLightTheme);
            break;

        case IDC_BTN_SPELL_EXCLUSIONS:
            ClassicSpellExclusionsDialog::Show(hInstance_, hwnd_, systemConfig_.forceLightTheme);
            // Reload config to pick up changes made in the dialog
            config_ = ConfigManager::LoadOrDefault();
            break;

        case IDC_BTN_MACRO_TABLE:
            ClassicMacroTableDialog::Show(hInstance_, hwnd_, systemConfig_.forceLightTheme);
            break;

        case IDC_BTN_CHECK_UPDATE: {
            HWND dlgHwnd = hwnd_;

            PostMessageW(dlgHwnd, WM_UPDATE_BTN_STATE, 1, 0);

            // Background check with progress dialog
            struct State {
                std::atomic<bool> done{false};
                UpdateInfo info;
            };
            auto state = std::make_shared<State>();

            std::thread([state]() {
                state->info = UpdateChecker::CheckForUpdate();
                state->done.store(true, std::memory_order_release);
            }).detach();

            bool completed = UpdateChecker::ShowProgressDialog(
                dlgHwnd, S(StringId::UPDATE_CHECKING), state->done);

            PostMessageW(dlgHwnd, WM_UPDATE_BTN_STATE, 0, 0);

            if (!completed) break;  // User cancelled

            if (state->info.available) {
                if (UpdateChecker::ShowUpdateDialog(dlgHwnd, state->info)) {
                    if (UpdateChecker::DownloadWithProgress(dlgHwnd, state->info.downloadUrl)) {
                        // Flush dirty config before quitting — PostQuitMessage bypasses
                        // WM_CLOSE, so the deferred save timer never fires.
                        if (configDirty_) {
                            KillTimer(hwnd_, kTimerDeferredSave);
                            SaveToToml();
                        }
                        // Signal main process to exit so the updater can replace files.
                        // Without this, updater waits 30s then proceeds while we're still running.
                        HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
                        if (trayWnd) {
                            PostMessageW(trayWnd, WM_CLOSE, 0, 0);
                        }
                        PostQuitMessage(0);
                    }
                }
            } else if (state->info.checkSucceeded) {
                UpdateChecker::ShowUpToDateMessage(dlgHwnd);
            } else {
                UpdateChecker::ShowCheckFailedMessage(dlgHwnd);
            }
            break;
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// Parent-child toggle dependencies
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::UpdateSpellCheckChildren() {
    bool spellOn = (IsDlgButtonChecked(hwnd_, IDC_CHECK_SPELL) == BST_CHECKED);
    BOOL enable = spellOn ? TRUE : FALSE;

    // Child toggles: "Cho phép zwjf" and "Tự khôi phục phím sai"
    HWND zwjf = GetDlgItem(hwnd_, IDC_CHECK_ALLOW_ZWJF);
    HWND restore = GetDlgItem(hwnd_, IDC_CHECK_AUTO_RESTORE);
    HWND exclusions = GetDlgItem(hwnd_, IDC_BTN_SPELL_EXCLUSIONS);

    if (zwjf)       EnableWindow(zwjf, enable);
    if (restore)    EnableWindow(restore, enable);
    if (exclusions) EnableWindow(exclusions, enable);
}

// ════════════════════════════════════════════════════════════════════
// System toggle side effects
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnSystemToggle(const wchar_t* id, bool value) {
    if (wcscmp(id, L"run-startup") == 0) {
        RegisterRunOnStartup(value, systemConfig_.runAsAdmin);
    }
    else if (wcscmp(id, L"run-admin") == 0) {
        // Clean up startup mechanisms from the current user's context 
        // to ensure the exact user's HKCU or Scheduled Task is targeted.
        if (value) {
            RemoveRegistryStartup();
        } else {
            if (!RemoveScheduledTask()) {
                // UAC denied or removal failed — revert checkbox to match reality
                systemConfig_.runAsAdmin = true;
                CheckDlgButton(hwnd_, IDC_CHECK_RUN_ADMIN, BST_CHECKED);
                return;
            }
            if (systemConfig_.runAtStartup) {
                (void)SetRegistryStartup();
            }
        }

        // Flush to TOML now — RestartWithNewAdminMode reads config from disk
        KillTimer(hwnd_, kTimerDeferredSave);
        SaveToToml();
        // Restart main process to apply elevation change.
        // Startup registration handled by EnsureStartupRegistration() in new instance.
        HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
        if (trayWnd) {
            PostMessageW(trayWnd, WM_NEXUSKEY_RESTART, 0, 0);
        }
    }
    else if (wcscmp(id, L"desktop-shortcut") == 0) {
        SetDesktopShortcut(value);
    }
    else if (wcscmp(id, L"english-ui") == 0) {
        systemConfig_.language = value ? 1 : 0;
        SetLanguage(value ? Language::English : Language::Vietnamese);
        RefreshLabels();
        // Flush + notify tray to rebuild menu in new language
        KillTimer(hwnd_, kTimerDeferredSave);
        SaveToToml();
        HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
        if (trayWnd) PostMessageW(trayWnd, WM_NEXUSKEY_ICON_CHANGED, 0, 0);
    }
    else if (wcscmp(id, L"floating-icon") == 0) {
        // Flush TOML + notify main thread to show/hide floating icon immediately
        KillTimer(hwnd_, kTimerDeferredSave);
        SaveToToml();
        HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
        if (trayWnd) PostMessageW(trayWnd, WM_NEXUSKEY_ICON_CHANGED, 0, 0);
    }
    else if (wcscmp(id, L"force-light-theme") == 0) {
        // Re-init theme with new setting, repaint entire window
        theme_.Destroy();
        theme_.Init(hwnd_, value);
        SetFontOnAllChildren();
        theme_.ApplyWindowAttributes(hwnd_);
        theme_.ThemeAllChildren(hwnd_);
        InvalidateRect(hwnd_, nullptr, TRUE);
        // Flush to TOML immediately
        KillTimer(hwnd_, kTimerDeferredSave);
        SaveToToml();
    }
    // show-on-startup, check-update: saved to config,
    // main_lite.cpp reads updated config when dialog closes.
}

// ════════════════════════════════════════════════════════════════════
// Icon color picker (ChooseColor API)
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::OnPickIconColors() {
    auto result = ClassicIconColorDialog::Show(
        hInstance_, hwnd_,
        systemConfig_.customColorV, systemConfig_.customColorE,
        systemConfig_.forceLightTheme);

    if (result.accepted) {
        systemConfig_.customColorV = result.colorV;
        systemConfig_.customColorE = result.colorE;
        // Flush + notify immediately so tray icon updates
        KillTimer(hwnd_, kTimerDeferredSave);
        SaveToToml();
        HWND trayWnd = FindWindowW(L"NexusKeyTrayClass", nullptr);
        if (trayWnd) PostMessageW(trayWnd, WM_NEXUSKEY_ICON_CHANGED, 0, 0);
    }
}

// ════════════════════════════════════════════════════════════════════
// Language refresh — update all visible labels to current language
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::RefreshLabels() {
    bool en = (GetLanguage() == Language::English);

    // Tab names
    if (tabControl_) {
        TCITEMW tie{};
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<wchar_t*>(en ? L"Input" : L"Bộ Gõ");
        TabCtrl_SetItem(tabControl_, 0, &tie);
        tie.pszText = const_cast<wchar_t*>(en ? L"Macros" : L"Gõ Tắt");
        TabCtrl_SetItem(tabControl_, 1, &tie);
        tie.pszText = const_cast<wchar_t*>(en ? L"System" : L"Hệ thống");
        TabCtrl_SetItem(tabControl_, 2, &tie);
        InvalidateRect(tabControl_, nullptr, TRUE);
    }

    // Compact section labels
    SetDlgItemTextW(hwnd_, 2999, en ? L"  Basic  " : L"  Cơ bản  ");
    SetDlgItemTextW(hwnd_, IDC_STATIC_METHOD, en ? L"Input method" : L"Kiểu gõ");
    SetDlgItemTextW(hwnd_, IDC_STATIC_ENCODING, en ? L"Encoding" : L"Bảng mã");
    SetDlgItemTextW(hwnd_, IDC_STATIC_SWITCHKEY, en ? L"Switch key (V/E)" : L"Phím chuyển (Việt/Anh)");

    // Metadata-driven controls (checkboxes, buttons, dropdown labels)
    for (size_t i = 0; i < kSettingsCount && i < kMaxControls; ++i) {
        const auto& meta = kSettings[i];
        const wchar_t* lbl = (en && meta.labelEn && meta.labelEn[0]) ? meta.labelEn : meta.label;

        if (HWND ctrl = checkControls_[i]) {
            if (meta.type == SettingType::Toggle || meta.type == SettingType::Action)
                SetWindowTextW(ctrl, lbl);
        }
        if (HWND lbl2 = extraControls_[i]) {
            if (meta.type == SettingType::Dropdown)
                SetWindowTextW(lbl2, lbl);
        }
    }

    // Icon style dropdown items
    HWND iconCombo = GetDlgItem(hwnd_, IDC_COMBO_ICON_STYLE);
    if (iconCombo) {
        int sel = ComboBox_GetCurSel(iconCombo);
        ComboBox_ResetContent(iconCombo);
        ComboBox_AddString(iconCombo, en ? L"Default color" : L"Màu mặc định");
        ComboBox_AddString(iconCombo, en ? L"Dark" : L"Nền tối");
        ComboBox_AddString(iconCombo, en ? L"Light" : L"Nền sáng");
        ComboBox_AddString(iconCombo, en ? L"Custom" : L"Tự chọn");
        if (sel >= 0) ComboBox_SetCurSel(iconCombo, sel);
    }

    // Startup mode dropdown items
    HWND startupCombo = GetDlgItem(hwnd_, IDC_COMBO_STARTUP_MODE);
    if (startupCombo) {
        int sel = ComboBox_GetCurSel(startupCombo);
        ComboBox_ResetContent(startupCombo);
        ComboBox_AddString(startupCombo, en ? L"Vietnamese" : L"Tiếng Việt");
        ComboBox_AddString(startupCombo, en ? L"English" : L"Tiếng Anh");
        ComboBox_AddString(startupCombo, en ? L"Remember" : L"Ghi nhớ");
        if (sel >= 0) ComboBox_SetCurSel(startupCombo, sel);
    }

    // Report bug link
    if (linkReportBug_) {
        SetWindowTextW(linkReportBug_, en
            ? L"<a href=\"https://github.com/phatMT97/NexusKey/issues\">Report bug</a>"
            : L"<a href=\"https://github.com/phatMT97/NexusKey/issues\">Báo cáo lỗi</a>");
    }

    // Tooltips
    if (tooltip_) {
        for (size_t i = 0; i < kSettingsCount; ++i) {
            const auto& meta = kSettings[i];
            if (meta.win32Id == 0) continue;

            const wchar_t* tip = en ? meta.tooltipEn : meta.tooltip;
            if (!tip) continue;

            HWND ctrl = GetDlgItem(hwnd_, meta.win32Id);
            if (!ctrl) continue;

            TTTOOLINFOW ti{};
            ti.cbSize   = sizeof(ti);
            ti.uFlags   = TTF_IDISHWND;
            ti.hwnd     = hwnd_;
            ti.uId      = reinterpret_cast<UINT_PTR>(ctrl);
            ti.lpszText = const_cast<wchar_t*>(tip);
            SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
        }
    }

    InvalidateRect(hwnd_, nullptr, TRUE);
}

// ════════════════════════════════════════════════════════════════════
// Tooltips
// ════════════════════════════════════════════════════════════════════

void ClassicSettingsDialog::SetupTooltips() {
    tooltip_ = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd_, nullptr, hInstance_, nullptr);

    if (!tooltip_) return;

    SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, Dpi(300));
    SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_INITIAL, 400);

    for (size_t i = 0; i < kSettingsCount; ++i) {
        const auto& meta = kSettings[i];
        if (!meta.tooltip || meta.win32Id == 0) continue;

        HWND ctrl = GetDlgItem(hwnd_, meta.win32Id);
        if (!ctrl) continue;

        TTTOOLINFOW ti{};
        ti.cbSize   = sizeof(ti);
        ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd     = hwnd_;
        ti.uId      = reinterpret_cast<UINT_PTR>(ctrl);
        ti.lpszText = const_cast<wchar_t*>(meta.tooltip);

        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }

    ThemeTooltip();
}

void ClassicSettingsDialog::ThemeTooltip() {
    if (!tooltip_) return;

    if (theme_.IsDark()) {
        SendMessageW(tooltip_, TTM_SETTIPBKCOLOR, static_cast<WPARAM>(theme_.Colors().surface), 0);
        SendMessageW(tooltip_, TTM_SETTIPTEXTCOLOR, static_cast<WPARAM>(theme_.Colors().text), 0);
    } else {
        SendMessageW(tooltip_, TTM_SETTIPBKCOLOR, static_cast<WPARAM>(GetSysColor(COLOR_INFOBK)), 0);
        SendMessageW(tooltip_, TTM_SETTIPTEXTCOLOR, static_cast<WPARAM>(GetSysColor(COLOR_INFOTEXT)), 0);
    }
}

// ════════════════════════════════════════════════════════════════════
// Control creation helpers
// ════════════════════════════════════════════════════════════════════

HWND ClassicSettingsDialog::CreateLabel(const wchar_t* text, int x, int y, int w, int h, UINT id) {
    return CreateWindowExW(
        0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateCombo(int x, int y, int w, int h, UINT id) {
    return CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateCheck(const wchar_t* text, int x, int y, int w, int h, UINT id) {
    return CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateEdit(int x, int y, int w, int h, UINT id) {
    return CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

HWND ClassicSettingsDialog::CreateBtn(const wchar_t* text, int x, int y, int w, int h, UINT id, bool isPrimary) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    style |= isPrimary ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON;

    return CreateWindowExW(
        0, L"BUTTON", text,
        style,
        x, y, w, h,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInstance_, nullptr
    );
}

void ClassicSettingsDialog::SetFontOnAllChildren() {
    EnumChildWindows(hwnd_, SetFontProc, reinterpret_cast<LPARAM>(this));
}

BOOL CALLBACK ClassicSettingsDialog::SetFontProc(HWND hwnd, LPARAM lParam) {
    auto* self = reinterpret_cast<ClassicSettingsDialog*>(lParam);
    if (!self) return TRUE;

    HFONT font = self->theme_.Fonts().body;
    if (GetDlgCtrlID(hwnd) == 2999) {
        font = self->theme_.Fonts().bodyBold;
    } else if (GetDlgCtrlID(hwnd) == IDC_EDIT_SWITCH_KEY && self->fontSmall_) {
        font = self->fontSmall_;
    }

    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return TRUE;
}

int ClassicSettingsDialog::Dpi(int value) const noexcept {
    return Classic::DpiScale(value, dpi_);
}

// ════════════════════════════════════════════════════════════════════
// Window procedure
// ════════════════════════════════════════════════════════════════════

LRESULT CALLBACK ClassicSettingsDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ClassicSettingsDialog* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ClassicSettingsDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ClassicSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND:
            self->OnCommand(wParam, lParam);
            return 0;

        case WM_NEXUSKEY_CONFIG_CHANGED:
            self->LoadSettings();
            self->PopulateControls();
            return 0;

        case WM_UPDATE_BTN_STATE: {
            HWND btn = GetDlgItem(hwnd, IDC_BTN_CHECK_UPDATE);
            if (!btn) break;
            if (wParam == 0) {
                EnableWindow(btn, TRUE);
                // SetWindowTextW(btn, S(StringId::UPDATE_CHECK_NOW));
            } else if (wParam == 1) {
                EnableWindow(btn, FALSE);
                // SetWindowTextW(btn, S(StringId::UPDATE_CHECKING));
            } else if (wParam == 2) {
                EnableWindow(btn, FALSE);
                // SetWindowTextW(btn, S(StringId::UPDATE_DOWNLOADING));
            }
            return 0;
        }

        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->idFrom == IDC_TAB_ADVANCED && hdr->code == TCN_SELCHANGE) {
                self->OnTabChange();
            }
            if (hdr->idFrom == IDC_LINK_REPORT_BUG && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
                ShellExecuteW(nullptr, L"open",
                    L"https://github.com/phatMT97/NexusKey/issues",
                    nullptr, nullptr, SW_SHOW);
            }
            return 0;
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

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            UINT ctrlId = static_cast<UINT>(wParam);

            // Only tab control is owner-drawn
            if (ctrlId == IDC_TAB_ADVANCED) {
                self->theme_.DrawTabItem(dis);
                return TRUE;
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);

            if (self->editHotkey_) {
                // Draw outer group box around the hotkey section
                RECT rcEx;
                GetWindowRect(self->editHotkey_, &rcEx);
                MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&rcEx), 2);

                HPEN pen = CreatePen(PS_SOLID, 1, self->theme_.Colors().border);
                HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                HPEN oldPen = (HPEN)SelectObject(hdc, pen);

                int offset = self->Dpi(8);
                int gbY = self->Dpi(10) + offset;
                int bottom = rcEx.bottom + self->Dpi(8);
                int r = self->theme_.CornerRadius();
                RoundRect(hdc, self->Dpi(8), gbY, self->Dpi(kAdvancedWidth - 8), bottom, r, r);

                SelectObject(hdc, oldBr);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);

                // Draw rounded border for hotkey edit
                self->theme_.DrawHotkeyEditBorder(hdc, hwnd, self->editHotkey_, self->Dpi(kControlHeight));
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
            if (self->editHotkey_ && GetFocus() == self->editHotkey_) {
                SetFocus(hwnd);
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (self->theme_.OnSettingChange(lParam)) {
                self->theme_.ApplyWindowAttributes(hwnd);
                self->theme_.ThemeAllChildren(hwnd);
                self->ThemeTooltip();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;

        case WM_TIMER:
            if (wParam == kTimerDeferredSave) {
                KillTimer(hwnd, kTimerDeferredSave);
                if (self->configDirty_)
                    self->SaveToToml();
            }
            return 0;

        case WM_CLOSE:
            if (self->configDirty_) {
                KillTimer(hwnd, kTimerDeferredSave);
                self->SaveToToml();
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            if (self->fontSmall_) {
                DeleteObject(self->fontSmall_);
                self->fontSmall_ = nullptr;
            }
            auto restoreAnim = [](HWND combo) {
                if (combo && GetPropW(combo, L"WasAnim")) {
                    SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0, (PVOID)TRUE, 0);
                    RemovePropW(combo, L"WasAnim");
                }
            };
            restoreAnim(self->comboMethod_);
            restoreAnim(self->comboEncoding_);

            self->theme_.Destroy();
            self->hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace NextKey::Classic
