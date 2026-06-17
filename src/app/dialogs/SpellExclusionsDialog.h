// VKey - Spell Check Exclusions Dialog Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "SciterSubDialog.h"
#include <string>
#include <vector>

namespace NextKey {

/// Dialog for managing spell check exclusion prefixes (Sciter subdialog)
class SpellExclusionsDialog : public SciterSubDialog {
public:
    SpellExclusionsDialog(HWND parent);

    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

private:
    void populateList();
    void addEntry(const std::wstring& text);
    void removeEntry(const std::wstring& text);
    void importExclusions();
    void exportExclusions();
    void persistAndSignal();

    std::vector<std::wstring> entries_;
};

}  // namespace NextKey
