// VKey - About Dialog Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "AboutDialog.h"

namespace NextKey {

AboutDialog::AboutDialog(HWND parent)
    : SciterSubDialog({
        L"this://app/about/about.html",
        L"VKey - About",
        300, 340, parent, true, 36, 40, true
    }) {
}

}  // namespace NextKey
