// VKey - Input Engine Factory Implementation
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial
// Dual-licensed: AGPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.

#include "EngineFactory.h"
#include "TypingEngine.h"

namespace NextKey {

std::unique_ptr<IInputEngine> EngineFactory::Create(const TypingConfig& config) {
    // All input methods route through TypingEngine (unified engine).
    // Mode dispatch happens inside TypingEngine via IsTelexMode()/IsVniMode().
    return std::make_unique<TypingEngine>(config);
}

std::unique_ptr<IInputEngine> EngineFactory::Create(InputMethod method) {
    TypingConfig config;
    config.inputMethod = method;
    return Create(config);
}

}  // namespace NextKey
