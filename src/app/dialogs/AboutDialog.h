// VKey - About Dialog Header
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "SciterSubDialog.h"

namespace NextKey {

/// About dialog (Sciter subdialog) — shows credits, repo links
class AboutDialog : public SciterSubDialog {
public:
    explicit AboutDialog(HWND parent);
};

}  // namespace NextKey
