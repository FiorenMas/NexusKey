// NexusKey - Hotkey Manager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "HotkeyManager.h"
#include "HookEngine.h"  // NEXUSKEY_EXTRA_INFO tag
#include "core/CrashLog.h"
#include "core/Debug.h"

namespace NextKey {

std::atomic<HotkeyManager*> HotkeyManager::s_instance{nullptr};

HotkeyManager::~HotkeyManager() {
    Uninstall();
}

BYTE HotkeyManager::ResolveVk(wchar_t key) noexcept {
    if (key == 0) return 0;
    SHORT r = VkKeyScanW(key);
    return (r == -1) ? 0 : LOBYTE(r);
}

HotkeyManager::SlotId HotkeyManager::AddHotkey(const HotkeyConfig& config, Callback callback) {
    std::lock_guard lk(slotsMutex_);
    slots_.push_back(Slot{config, std::move(callback), ResolveVk(config.key), false});
    return slots_.size() - 1;
}

void HotkeyManager::UpdateHotkey(SlotId slot, const HotkeyConfig& config) {
    std::lock_guard lk(slotsMutex_);
    if (slot >= slots_.size()) return;
    // Skip if config unchanged — preserves comboKeyDown across spurious reloads.
    // Without this, a config reload while the user holds the combo resets
    // comboKeyDown=false and the next auto-repeat re-fires the callback.
    if (slots_[slot].config == config) return;
    slots_[slot].config = config;
    slots_[slot].vkCached = ResolveVk(config.key);
    slots_[slot].comboKeyDown = false;
}

void HotkeyManager::Initialize(HINSTANCE hInstance) {
    s_instance = this;
    InstallKeyboardHook(hInstance);
    NEXTKEY_LOG(L"HotkeyManager installed (%zu slot%s)",
                slots_.size(), slots_.size() == 1 ? L"" : L"s");
}

void HotkeyManager::Uninstall() {
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
    std::lock_guard lk(slotsMutex_);
    slots_.clear();
}

void HotkeyManager::InstallKeyboardHook(HINSTANCE hInstance) {
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    if (keyboardHook_) {
        NEXTKEY_LOG(L"Keyboard hook installed for hotkey");
    } else {
        NEXTKEY_LOG(L"Keyboard hook failed (error: %lu)", GetLastError());
    }
}

// Inject VK_LCONTROL down+up tagged with NEXUSKEY_EXTRA_INFO to break Windows
// "Alt/Win tapped alone" detection. Without this, releasing Alt before the key
// in combos like Alt+Z activates the browser menu bar (Firefox) or steals focus
// (Chrome). HookEngine::LowLevelKeyboardProc passes through events with this tag.
void HotkeyManager::InjectDummyKey() noexcept {
    static const WORD scan = static_cast<WORD>(MapVirtualKeyW(VK_LCONTROL, MAPVK_VK_TO_VSC));
    INPUT inputs[2] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LCONTROL;
    inputs[0].ki.wScan = scan;
    inputs[0].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_LCONTROL;
    inputs[1].ki.wScan = scan;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[1].ki.dwExtraInfo = HookEngine::NEXUSKEY_EXTRA_INFO;

    SendInput(2, inputs, sizeof(INPUT));
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    try {
        HotkeyManager* inst = s_instance.load(std::memory_order_relaxed);
        if (nCode != HC_ACTION || !inst) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        auto& self = *inst;
        auto* pKey = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Pass through our own injected dummy events untouched.
        if (pKey->dwExtraInfo == HookEngine::NEXUSKEY_EXTRA_INFO) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        const DWORD vk = pKey->vkCode;

        const bool isCtrl = (vk == VK_LCONTROL || vk == VK_RCONTROL);
        const bool isShift = (vk == VK_LSHIFT || vk == VK_RSHIFT);
        const bool isAlt = (vk == VK_LMENU || vk == VK_RMENU);
        const bool isWin = (vk == VK_LWIN || vk == VK_RWIN);
        const bool isModifier = isCtrl || isShift || isAlt || isWin;

        // Snapshot pre-update state so modifier-only release checks see the modifier
        // as "still held" (match the original per-modifier semantics).
        const bool preCtrl = self.modCtrlDown_;
        const bool preShift = self.modShiftDown_;
        const bool preAlt = self.modAltDown_;
        const bool preWin = self.modWinDown_;
        const bool preOtherKey = self.otherKeyPressed_;

        // Update modifier state. Reset otherKeyPressed_ on modifier down-transition.
        if (isCtrl) {
            if (isDown && !self.modCtrlDown_) { self.modCtrlDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) self.modCtrlDown_ = false;
        } else if (isShift) {
            if (isDown && !self.modShiftDown_) { self.modShiftDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) self.modShiftDown_ = false;
        } else if (isAlt) {
            if (isDown && !self.modAltDown_) { self.modAltDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) self.modAltDown_ = false;
        } else if (isWin) {
            if (isDown && !self.modWinDown_) { self.modWinDown_ = true; self.otherKeyPressed_ = false; }
            else if (isUp) self.modWinDown_ = false;
        } else if (isDown) {
            self.otherKeyPressed_ = true;
        }

        // Strict XOR: required modifiers must be held AND non-required modifiers
        // must NOT be held. Prevents Alt+Z hotkey from firing on Ctrl+Alt+Z.
        auto matchCombo = [&](const HotkeyConfig& cfg) noexcept {
            return cfg.ModifiersMatch(self.modCtrlDown_, self.modShiftDown_,
                                      self.modAltDown_, self.modWinDown_);
        };

        auto matchModifierOnlyRelease = [&](const HotkeyConfig& cfg) noexcept {
            return cfg.ModifiersMatch(preCtrl, preShift, preAlt, preWin);
        };

        std::lock_guard lk(self.slotsMutex_);

        // ─── Combo hotkey: target key DOWN fires, UP is eaten ───
        if (!isModifier && (isDown || isUp)) {
            for (auto& slot : self.slots_) {
                if (slot.vkCached == 0) continue;  // Modifier-only slot
                if (vk != static_cast<DWORD>(slot.vkCached)) continue;

                if (isDown) {
                    if (slot.comboKeyDown) return 1;  // Eat auto-repeat
                    if (matchCombo(slot.config)) {
                        slot.comboKeyDown = true;
                        if (slot.callback) slot.callback();
                        if (slot.config.alt || slot.config.win) InjectDummyKey();
                        return 1;  // Eat DOWN
                    }
                } else {  // isUp
                    if (slot.comboKeyDown) {
                        slot.comboKeyDown = false;
                        return 1;  // Eat matching UP
                    }
                }
            }
        }

        // ─── Modifier-only hotkey: fires on modifier UP if no non-modifier was pressed ───
        if (isModifier && isUp && !preOtherKey) {
            for (auto& slot : self.slots_) {
                if (slot.vkCached != 0) continue;  // Combo slot
                const auto& c = slot.config;
                if (!c.HasAny()) continue;  // Empty config would match everything
                if (matchModifierOnlyRelease(c)) {
                    if (slot.callback) slot.callback();
                }
            }
        }
    } catch (const std::exception& e) {
        CrashLog(L"HotkeyManager::LowLevelKeyboardProc", e.what());
    } catch (...) {
        CrashLog(L"HotkeyManager::LowLevelKeyboardProc", "(non-std exception)");
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace NextKey
