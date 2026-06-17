// VKey - Excluded Apps Dialog Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "WindowPickerDialog.h"
#include <string>
#include <vector>
#include <utility>

namespace NextKey {

/// Dialog for managing excluded apps list (Sciter subdialog)
class ExcludedAppsDialog : public WindowPickerDialog {
public:
    ExcludedAppsDialog(HWND parent);

    bool handle_event(HELEMENT he, BEHAVIOR_EVENT_PARAMS& params) override;

protected:
    void onWindowPicked(const std::wstring& exeName) override;

private:
    void populateList();
    void addApp(const std::wstring& name, int mode);
    void setMode(const std::wstring& name, int mode);
    void removeApp(const std::wstring& name);
    void persistAndSignal();
    void importApps();
    void exportApps();

    // Per-app mode lock: 0 = E (excluded/transparent), 1 = V (force Vietnamese).
    static constexpr int kModeE = 0;
    static constexpr int kModeV = 1;
    std::vector<std::pair<std::wstring, int>> appList_;
};

}  // namespace NextKey
