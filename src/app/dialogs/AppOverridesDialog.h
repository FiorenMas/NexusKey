// VKey - App Overrides Dialog Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "WindowPickerDialog.h"
#include "core/config/ConfigManager.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace NextKey {

/// Dialog for managing per-app configuration (encoding + input method overrides)
class AppOverridesDialog : public WindowPickerDialog {
public:
    AppOverridesDialog(HWND parent);

    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

protected:
    void onWindowPicked(const std::wstring& exeName) override;

private:
    void populateList();
    void addEntry(const std::wstring& name, int8_t encoding, int8_t inputMethod, int8_t sendMethod);
    void removeEntry(const std::wstring& name);
    void persistAndSignal();

    std::unordered_map<std::wstring, AppOverrideEntry> entries_;
};

}  // namespace NextKey
