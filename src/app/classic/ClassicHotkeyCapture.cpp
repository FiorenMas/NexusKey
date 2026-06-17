// VKey Classic — Shared hotkey capture modal implementation.
// SPDX-License-Identifier: AGPL-3.0-only

#include "ClassicHotkeyCapture.h"

#ifdef _WIN32

#include "core/CrashLog.h"
#include "core/hotkey/HotkeyLabel.h"
#include "core/hotkey/HotkeyRegistry.h"  // kMod* constants
#include "app/helpers/AppHelpers.h"

#include <exception>
#include <string>

namespace NextKey::Classic {

namespace {

constexpr DWORD kDoubleTapWindowMs = 400;

[[nodiscard]] uint32_t CanonicalVk(uint32_t vk) noexcept {
    switch (vk) {
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LSHIFT:   case VK_RSHIFT:   return VK_SHIFT;
    case VK_LMENU:    case VK_RMENU:    return VK_MENU;
    case VK_RWIN:                       return VK_LWIN;
    default:                            return vk;
    }
}

[[nodiscard]] bool IsModifierVk(uint32_t vk) noexcept {
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU
        || vk == VK_LWIN  || vk == VK_RWIN;
}

[[nodiscard]] std::wstring PreviewLabel(uint32_t vk, uint32_t mods, bool doubleTap) {
    if (doubleTap) {
        // 2× implies mods=0 by construction; format only the key name.
        return L"2×" + FormatHotkeyLabel(vk, 0);
    }
    if (mods == 0 && IsModifierVk(vk)) {
        // Modifier-alone (only reachable when allowBareModifier=true).
        switch (vk) {
        case VK_CONTROL: return L"Ctrl (giữ-thả)";
        case VK_MENU:    return L"Alt (giữ-thả)";
        case VK_SHIFT:   return L"Shift (giữ-thả)";
        case VK_LWIN:    return L"Win (giữ-thả)";
        }
    }
    return FormatHotkeyLabel(vk, mods);
}

constexpr const wchar_t* kClassName  = L"VKeyClassicHotkeyCapture";
constexpr const wchar_t* kDefaultTitle  = L"Ghi nhận phím";
constexpr const wchar_t* kDefaultPrompt =
    L"Nhấn phím hoặc tổ hợp phím cần gán. Nhả nhanh modifier 2 lần để gán \"2×\".";

struct CaptureOverlay {
    HWND     hwnd       = nullptr;
    HWND     preview    = nullptr;
    HWND     btnCancel  = nullptr;
    ClassicTheme* theme = nullptr;
    HotkeyCaptureOptions opts{};

    HotkeyCaptureResult captured{};
    bool     ok         = false;
    bool     done       = false;
    uint32_t pendingModVk = 0;
    DWORD    pendingModTs = 0;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) try {
        CaptureOverlay* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = reinterpret_cast<CaptureOverlay*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd = hwnd;
        } else {
            self = reinterpret_cast<CaptureOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const uint32_t vk = CanonicalVk(static_cast<uint32_t>(wParam));
            // Bare-Esc → cancel (when no modifiers are held).
            if (vk == VK_ESCAPE) {
                const bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
                const bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
                const bool win   = ((GetAsyncKeyState(VK_LWIN)   & 0x8000) != 0)
                                || ((GetAsyncKeyState(VK_RWIN)   & 0x8000) != 0);
                if (!ctrl && !shift && !alt && !win) {
                    self->ok = false;
                    self->done = true;
                    DestroyWindow(hwnd);
                    return 0;
                }
            }
            if (IsModifierVk(vk)) {
                // Modifier DOWN — live preview "Shift+…" so user sees the
                // modifier was detected even before the chord key arrives.
                // Commit logic still waits for KEYUP (modifier-alone /
                // double-tap) or KEYDOWN of a non-modifier (chord).
                uint32_t heldMods = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) heldMods |= kModCtrl;
                if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) heldMods |= kModShift;
                if (GetAsyncKeyState(VK_MENU)    & 0x8000) heldMods |= kModAlt;
                if ((GetAsyncKeyState(VK_LWIN)   & 0x8000)
                 || (GetAsyncKeyState(VK_RWIN)   & 0x8000)) heldMods |= kModWin;
                std::wstring prefix = FormatHotkeyLabel(0, heldMods);
                SetWindowTextW(self->preview,
                    (prefix.empty() ? std::wstring(L"…")
                                    : prefix + L"+…").c_str());
                return 0;
            }

            uint32_t mods = 0;
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= kModCtrl;
            if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= kModShift;
            if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= kModAlt;
            if ((GetAsyncKeyState(VK_LWIN)   & 0x8000)
             || (GetAsyncKeyState(VK_RWIN)   & 0x8000)) mods |= kModWin;
            self->captured = HotkeyCaptureResult{vk, mods, /*doubleTap=*/false};
            self->ok = true;
            self->done = true;
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const uint32_t vk = CanonicalVk(static_cast<uint32_t>(wParam));
            if (!IsModifierVk(vk)) return 0;

            const DWORD now = GetTickCount();
            const bool isSecondTap = (self->pendingModVk == vk)
                                  && (now - self->pendingModTs <= kDoubleTapWindowMs);
            if (isSecondTap && self->opts.allowDoubleTap) {
                KillTimer(hwnd, 1);
                self->captured = HotkeyCaptureResult{vk, 0, /*doubleTap=*/true};
                self->ok = true;
                self->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            // Modifier-combo (other modifier(s) held while this one was released).
            uint32_t otherMods = 0;
            if (vk != VK_CONTROL && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) otherMods |= kModCtrl;
            if (vk != VK_SHIFT   && (GetAsyncKeyState(VK_SHIFT)   & 0x8000)) otherMods |= kModShift;
            if (vk != VK_MENU    && (GetAsyncKeyState(VK_MENU)    & 0x8000)) otherMods |= kModAlt;
            if (vk != VK_LWIN
                && ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))) {
                otherMods |= kModWin;
            }
            if (otherMods != 0 && self->opts.allowBareModifier) {
                self->captured = HotkeyCaptureResult{vk, otherMods, /*doubleTap=*/false};
                self->ok = true;
                self->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            // Pure modifier-alone candidate — only meaningful when allowBareModifier.
            if (self->opts.allowBareModifier) {
                self->pendingModVk = vk;
                self->pendingModTs = now;
                SetTimer(hwnd, 1, kDoubleTapWindowMs + 20, nullptr);
                SetWindowTextW(self->preview,
                    PreviewLabel(vk, 0, /*doubleTap=*/false).c_str());
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam != 1 || self->pendingModVk == 0) return 0;
            KillTimer(hwnd, 1);
            if (!self->opts.allowBareModifier) {
                // Shouldn't normally fire — guard against accidental wiring.
                self->pendingModVk = 0;
                return 0;
            }
            self->captured = HotkeyCaptureResult{
                self->pendingModVk, 0, /*doubleTap=*/false};
            self->ok = true;
            self->done = true;
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == 1) {  // cancel button HMENU id
                self->ok = false;
                self->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            if (self->theme) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                RECT rc; GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, self->theme->BrushBackground());
                return 1;
            }
            break;

        case WM_CTLCOLORSTATIC:
            if (self->theme) {
                return reinterpret_cast<LRESULT>(self->theme->OnCtlColorStatic(
                    reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
            }
            break;
        case WM_CTLCOLORBTN:
            if (self->theme) {
                return reinterpret_cast<LRESULT>(self->theme->OnCtlColorBtn(
                    reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
            }
            break;

        case WM_CLOSE:
            self->ok = false;
            self->done = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } catch (const std::exception& e) {
        NextKey::CrashLog(L"ClassicHotkeyCapture::WndProc", e.what());
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } catch (...) {
        NextKey::CrashLog(L"ClassicHotkeyCapture::WndProc", "(non-std exception)");
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

}  // namespace

std::optional<HotkeyCaptureResult> ShowHotkeyCaptureDialog(
    HINSTANCE hInstance, HWND parent, ClassicTheme& theme, UINT dpi,
    const HotkeyCaptureOptions& opts) {

    // RegisterClassExW is idempotent — duplicate registrations return 0
    // with GetLastError() == ERROR_CLASS_ALREADY_EXISTS, which is harmless.
    // No need to guard with a static bool (which would also swallow real
    // first-call failures).
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = CaptureOverlay::WndProc;
    wc.cbWndExtra    = sizeof(void*);
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm       = wc.hIcon;
    RegisterClassExW(&wc);

    CaptureOverlay self;
    self.theme = &theme;
    self.opts  = opts;

    auto Dpi = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };

    const int w = Dpi(360);
    const int h = Dpi(160);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, w, h};
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    const int aw = rc.right - rc.left, ah = rc.bottom - rc.top;
    POINT pt = NextKey::GetCenteredPos(parent, aw, ah);

    const wchar_t* title = opts.titleText  ? opts.titleText  : kDefaultTitle;
    const wchar_t* prompt = opts.promptText ? opts.promptText : kDefaultPrompt;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, title,
        style, pt.x, pt.y, aw, ah, parent, nullptr, hInstance, &self);
    if (!hwnd) return std::nullopt;
    theme.ApplyWindowAttributes(hwnd);

    const int pad = Dpi(14);
    const int rowH = Dpi(20);
    HWND promptStatic = CreateWindowExW(0, L"STATIC", prompt,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        pad, pad, w - pad * 2, rowH * 2, hwnd, nullptr, hInstance, nullptr);
    (void)promptStatic;

    self.preview = CreateWindowExW(0, L"STATIC", L"(chờ phím...)",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        pad, pad + rowH * 2 + Dpi(6), w - pad * 2, rowH, hwnd, nullptr, hInstance, nullptr);

    const int btnW = Dpi(80);
    const int btnH = Dpi(26);
    self.btnCancel = CreateWindowExW(0, L"BUTTON", L"Hủy (Esc)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        w - pad - btnW, h - pad - btnH, btnW, btnH,
        hwnd, reinterpret_cast<HMENU>(1), hInstance, nullptr);

    EnumChildWindows(hwnd, [](HWND h, LPARAM lp) -> BOOL {
        auto* t = reinterpret_cast<ClassicTheme*>(lp);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(t->Fonts().body), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&theme));
    theme.ThemeAllChildren(hwnd);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    EnableWindow(parent, FALSE);
    MSG msg{};
    while (!self.done && GetMessageW(&msg, nullptr, 0, 0)) {
        // Don't pass key messages through IsDialogMessage — it would eat
        // Tab/Enter/Esc/Alt before our WndProc sees them.
        if (msg.message != WM_KEYDOWN && msg.message != WM_KEYUP
         && msg.message != WM_SYSKEYDOWN && msg.message != WM_SYSKEYUP
         && IsDialogMessageW(hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (!self.ok) return std::nullopt;
    return self.captured;
}

}  // namespace NextKey::Classic

#endif
