// VKey - Spell Check Exclusions Dialog Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "SpellExclusionsDialog.h"
#include "helpers/AppHelpers.h"
#include "core/config/ConfigManager.h"
#include "core/WinStrings.h"
#include "sciter-x-dom.hpp"
#include "DialogUtils.h"
#include <algorithm>
#include <fstream>

using namespace sciter::dom;

namespace NextKey {

SpellExclusionsDialog::SpellExclusionsDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/spellexclusions/spellexclusions.html",
        L"VKey - Spell Exclusions",
        340, 380, parent, true, 36, 40, true
    }) {
    auto config = ConfigManager::LoadOrDefault();
    entries_ = std::move(config.spellExclusions);
    populateList();
}

void SpellExclusionsDialog::persistAndSignal() {
    auto path = ConfigManager::GetConfigPath();
    auto config = ConfigManager::LoadFromFile(path).value_or(TypingConfig{});
    config.spellExclusions = entries_;
    (void)ConfigManager::SaveToFile(path, config);
    SignalConfigChange();
}

bool SpellExclusionsDialog::handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) {
    if (params.cmd == BUTTON_CLICK) {
        element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"btn-close") {
            PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
            return true;
        }
    }

    if (params.cmd == VALUE_CHANGED) {
        element el(params.heTarget);
        std::wstring id = el.get_attribute("id");

        if (id == L"val-action") {
            sciter::value val = el.get_value();
            std::wstring action = val.is_string() ? val.get<std::wstring>() : L"";
            if (!action.empty()) {
                element root = get_root();
                element nameInput = root.find_first("#val-entry-name");
                std::wstring entryName;
                if (nameInput.is_valid()) {
                    sciter::value nv = nameInput.get_value();
                    entryName = nv.is_string() ? nv.get<std::wstring>() : L"";
                }

                if (action == L"add") {
                    if (!entryName.empty()) {
                        addEntry(entryName);
                    }
                } else if (action == L"delete") {
                    if (!entryName.empty()) {
                        removeEntry(entryName);
                    }
                } else if (action == L"import") {
                    importExclusions();
                } else if (action == L"export") {
                    exportExclusions();
                } else if (action == L"close") {
                    PostMessage(get_hwnd(), WM_CLOSE, 0, 0);
                }

                el.set_value(sciter::value(L""));
            }
            return true;
        }
    }

    return sciter::window::handle_event(he, params);
}

void SpellExclusionsDialog::populateList() {
    call_function("clearList");
    for (auto& entry : entries_) {
        call_function("addToList", sciter::value(entry.c_str()));
    }
    call_function("forceRefresh");
}

void SpellExclusionsDialog::addEntry(const std::wstring& text) {
    // Trim
    size_t start = 0, end = text.size();
    while (start < end && text[start] == L' ') ++start;
    while (end > start && text[end - 1] == L' ') --end;
    if (end - start < 2) return;

    std::wstring entry = text.substr(start, end - start);
    for (auto& ch : entry) ch = towlower(ch);  // Store pre-lowercased

    // Dedup
    for (auto& existing : entries_) {
        if (existing == entry) return;
    }

    entries_.push_back(entry);
    std::sort(entries_.begin(), entries_.end());
    populateList();
    persistAndSignal();
}

void SpellExclusionsDialog::removeEntry(const std::wstring& text) {
    auto it = std::find(entries_.begin(), entries_.end(), text);
    if (it != entries_.end()) {
        entries_.erase(it);
        call_function("removeFromList", sciter::value(text.c_str()));
        persistAndSignal();
    }
}

void SpellExclusionsDialog::importExclusions() {
    std::wstring path = ShowOpenFileDialogW(
        get_hwnd(),
        L"Text file (*.txt)\0*.txt\0All (*.*)\0*.*\0",
        L"txt"
    );
    if (path.empty()) return;

    int msgboxID = MessageBoxW(
        get_hwnd(),
        L"B\u1EA1n c\u00F3 mu\u1ED1n gi\u1EEF l\u1EA1i danh s\u00E1ch hi\u1EC7n t\u1EA1i kh\u00F4ng?",
        L"T\u1EEB kh\u00F3a lo\u1EA1i tr\u1EEB",
        MB_ICONEXCLAMATION | MB_YESNO
    );

    bool append = (msgboxID == IDYES);

    std::ifstream infile(path);
    if (!infile.is_open()) return;

    if (!append) {
        entries_.clear();
    }

    ParseConfigLines(infile, [&](const std::string& line) {
        std::wstring wName = Utf8ToWide(line);
        if (wName.empty()) return;
        for (auto& ch : wName) ch = towlower(ch);
        if (std::find(entries_.begin(), entries_.end(), wName) == entries_.end()) {
            entries_.push_back(wName);
        }
    });

    std::sort(entries_.begin(), entries_.end());
    populateList();
    persistAndSignal();
}

void SpellExclusionsDialog::exportExclusions() {
    std::wstring path = ShowSaveFileDialogW(
        get_hwnd(),
        L"Text file (*.txt)\0*.txt\0",
        L"txt",
        L"VKeySpellExclusions"
    );
    if (path.empty()) return;

    std::ofstream outfile(path);
    if (!outfile.is_open()) return;

    outfile << ";VKey Spell Exclusions\n";

    std::vector<std::wstring> sorted = entries_;
    std::sort(sorted.begin(), sorted.end());
    for (auto& entry : sorted) {
        outfile << WideToUtf8(entry) << "\n";
    }
}

}  // namespace NextKey
