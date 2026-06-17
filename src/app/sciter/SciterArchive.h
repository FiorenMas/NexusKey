// VKey - Sciter Archive Resource Binding
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

namespace NextKey {

/// Bind embedded Sciter resources (packed with packfolder.exe)
/// Call this ONCE before creating any Sciter windows
/// Only effective in Release builds with SCITER_USE_PACKFOLDER defined
void BindSciterResources();

}  // namespace NextKey
