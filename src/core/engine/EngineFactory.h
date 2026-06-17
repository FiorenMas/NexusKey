// VKey - Input Engine Factory
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
// Dual-licensed: AGPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.

#pragma once

#include "IInputEngine.h"
#include "core/config/TypingConfig.h"
#include <memory>

namespace NextKey {

/// Factory for creating input method engines
class EngineFactory {
public:
    /// Create an engine based on configuration
    [[nodiscard]] static std::unique_ptr<IInputEngine> Create(const TypingConfig& config);

    /// Create an engine for a specific input method
    [[nodiscard]] static std::unique_ptr<IInputEngine> Create(InputMethod method);
};

}  // namespace NextKey
