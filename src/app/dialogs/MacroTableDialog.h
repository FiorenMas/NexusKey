// VKey - Macro Table Dialog Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include <string>
#include <unordered_map>

namespace NextKey {

/// Dialog for managing macro/shorthand table (Sciter subdialog)
class MacroTableDialog : public SciterSubDialog {
public:
    MacroTableDialog(HWND parent);

    // Override event handler for VALUE_CHANGED on #val-action
    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

private:
    void populateList();
    void addMacro(const std::wstring& name, const std::wstring& content);
    void removeMacro(const std::wstring& name);
    void persistAndSignal();
    void importMacros();
    void exportMacros();

    std::unordered_map<std::wstring, std::wstring> macros_;
};

}  // namespace NextKey
