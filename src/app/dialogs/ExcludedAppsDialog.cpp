// VKey - Excluded Apps Dialog Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "ExcludedAppsDialog.h"
#include "DialogUtils.h"
#include "core/config/ConfigManager.h"
#include "helpers/AppHelpers.h"
#include "core/Strings.h"
#include "core/WinStrings.h"
#include "sciter-x-dom.hpp"
#include <algorithm>
#include <fstream>
#include <vector>

using namespace sciter::dom;

namespace NextKey {

ExcludedAppsDialog::ExcludedAppsDialog(HWND parent)
    : WindowPickerDialog({
        L"this://app/excludedapps/excludedapps.html",
        L"VKey - Excluded Apps",
        420, 420, parent, true, 36, 40, true
    }) {
    const auto path = ConfigManager::GetConfigPath();
    for (auto& e : ConfigManager::LoadAllExcludedApps(path)) {
        appList_.emplace_back(std::move(e), kModeE);
    }
    for (auto& v : ConfigManager::LoadForcedVnApps(path)) {
        // Disjoint by name (excluded wins) — mirror ConfigSnapshotBuilder.
        bool dup = false;
        for (auto& a : appList_) { if (a.first == v) { dup = true; break; } }
        if (!dup) appList_.emplace_back(std::move(v), kModeV);
    }
    std::sort(appList_.begin(), appList_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    populateList();
}

void ExcludedAppsDialog::persistAndSignal() {
    // Partition the tagged list back into the two TOML arrays.
    std::vector<std::wstring> excluded, forcedVn;
    for (auto& app : appList_) {
        (app.second == kModeV ? forcedVn : excluded).push_back(app.first);
    }
    const auto path = ConfigManager::GetConfigPath();
    (void)ConfigManager::SaveExcludedApps(path, excluded);
    (void)ConfigManager::SaveForcedVnApps(path, forcedVn);
    SignalConfigChange();
}

bool ExcludedAppsDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
    // Handle BUTTON_CLICK for close button
    if (params.cmd == BUTTON_CLICK) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"btn-close") {
            PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
            return true;
        }
    }

    // Handle VALUE_CHANGED for #val-action (triggered by JS triggerAction)
    if (params.cmd == VALUE_CHANGED) {
        sciter::dom::element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"val-action") {
            sciter::value val = el.get_value();
            std::wstring action = val.is_string() ? val.get<std::wstring>() : L"";
            if (!action.empty()) {
                // Read app name from hidden input
                sciter::dom::element root = get_root();
                sciter::dom::element nameInput = root.find_first("#val-app-name");
                std::wstring appName;
                if (nameInput.is_valid()) {
                    sciter::value nv = nameInput.get_value();
                    appName = nv.is_string() ? nv.get<std::wstring>() : L"";
                }
                // Read per-app mode (0 = E, 1 = V) from hidden input.
                int appMode = kModeE;
                sciter::dom::element modeInput = root.find_first("#val-app-mode");
                if (modeInput.is_valid()) {
                    sciter::value mv = modeInput.get_value();
                    if (mv.is_string()) {
                        appMode = (mv.get<std::wstring>() == L"1") ? kModeV : kModeE;
                    } else if (mv.is_int()) {
                        appMode = (mv.get<int>() == kModeV) ? kModeV : kModeE;
                    }
                }

                if (action == L"add-manual") {
                    if (!appName.empty()) {
                        addApp(appName, appMode);
                    }
                } else if (action == L"set-mode") {
                    if (!appName.empty()) {
                        setMode(appName, appMode);
                    }
                } else if (action == L"add-current") {
                    startWindowPicking();
                } else if (action == L"delete") {
                    if (!appName.empty()) {
                        removeApp(appName);
                    }
                } else if (action == L"get-running-apps") {
                    auto apps = getRunningApps();
                    sciter::value arr;
                    for (size_t i = 0; i < apps.size(); ++i) {
                        arr.set_item(static_cast<int>(i), sciter::value(apps[i].c_str()));
                    }
                    call_function("setRunningApps", arr);
                } else if (action == L"import") {
                    importApps();
                } else if (action == L"export") {
                    exportApps();
                } else if (action == L"close") {
                    PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
                }

                // Clear the action value to allow re-triggering
                el.set_value(sciter::value(L""));
            }
            return true;
        }
    }

    return sciter::window::handle_event(he, params);
}

void ExcludedAppsDialog::onWindowPicked(const std::wstring& exeName) {
    if (exeName == L"vkey.exe") {
        MessageBoxW(get_hwnd(), S(StringId::EXCLUDED_CANNOT_SELF),
                    L"VKey", MB_OK | MB_ICONWARNING);
    } else {
        addApp(exeName, kModeE);  // window picker defaults to E (exclude); toggle to V in-list
    }
}

void ExcludedAppsDialog::populateList() {
    call_function("clearAppList");
    for (auto& app : appList_) {
        call_function("addAppToList", sciter::value(app.first.c_str()),
                      sciter::value(app.second));
    }
    call_function("forceRefresh");
}

void ExcludedAppsDialog::addApp(const std::wstring& name, int mode) {
    std::wstring lower = ToLowerAscii(name);

    // Already present → just update its mode (an app is locked to one mode).
    for (auto& a : appList_) {
        if (a.first == lower) { setMode(lower, mode); return; }
    }

    appList_.emplace_back(lower, mode);
    call_function("addAppToList", sciter::value(lower.c_str()), sciter::value(mode));
    call_function("forceRefresh");
    persistAndSignal();
}

void ExcludedAppsDialog::setMode(const std::wstring& name, int mode) {
    std::wstring lower = ToLowerAscii(name);
    for (auto& a : appList_) {
        if (a.first == lower) {
            if (a.second != mode) {
                a.second = mode;
                call_function("setAppItemMode", sciter::value(lower.c_str()),
                              sciter::value(mode));
                persistAndSignal();
            }
            return;
        }
    }
}

void ExcludedAppsDialog::removeApp(const std::wstring& name) {
    std::wstring lower = ToLowerAscii(name);

    auto it = std::find_if(appList_.begin(), appList_.end(),
                           [&](const auto& a) { return a.first == lower; });
    if (it != appList_.end()) {
        appList_.erase(it);
        call_function("removeAppFromList", sciter::value(lower.c_str()));
        persistAndSignal();
    }
}

void ExcludedAppsDialog::importApps() {
    std::wstring path = ShowOpenFileDialogW(
        get_hwnd(),
        L"Text file (*.txt)\0*.txt\0All (*.*)\0*.*\0",
        L"txt"
    );
    if (path.empty()) return;

    int msgboxID = MessageBoxW(
        get_hwnd(),
        L"B\u1EA1n c\u00F3 mu\u1ED1n gi\u1EEF l\u1EA1i danh s\u00E1ch hi\u1EC7n t\u1EA1i kh\u00F4ng?",
        L"Danh s\u00E1ch lo\u1EA1i tr\u1EEB",
        MB_ICONEXCLAMATION | MB_YESNO
    );

    bool append = (msgboxID == IDYES);

    std::ifstream infile(path);
    if (!infile.is_open()) return;

    if (!append) {
        appList_.clear();
    }

    ParseConfigLines(infile, [&](const std::string& line) {
        // Per-app mode tag (D4): "name|V" → force-V, plain "name" → E.
        std::wstring entry = Utf8ToWide(line);
        int mode = kModeE;
        auto bar = entry.find_last_of(L'|');
        if (bar != std::wstring::npos) {
            if (ToLowerAscii(entry.substr(bar + 1)) == L"v") mode = kModeV;
            entry = entry.substr(0, bar);
        }
        std::wstring wName = ToLowerAscii(entry);
        if (wName.empty()) return;
        for (auto& a : appList_) {
            if (a.first == wName) { a.second = mode; return; }  // dedup → update mode
        }
        appList_.emplace_back(wName, mode);
    });

    std::sort(appList_.begin(), appList_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    populateList();
    persistAndSignal();
}

void ExcludedAppsDialog::exportApps() {
    std::wstring path = ShowSaveFileDialogW(
        get_hwnd(),
        L"Text file (*.txt)\0*.txt\0",
        L"txt",
        L"VKeyExcludedApps"
    );
    if (path.empty()) return;

    std::ofstream outfile(path);
    if (!outfile.is_open()) return;

    outfile << ";VKey Excluded Apps (name = English/excluded, name|V = force Vietnamese)\n";

    auto sorted = appList_;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& app : sorted) {
        outfile << WideToUtf8(app.first);
        if (app.second == kModeV) outfile << "|V";
        outfile << "\n";
    }
}

}  // namespace NextKey
