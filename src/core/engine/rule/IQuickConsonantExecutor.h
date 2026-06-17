// src/core/engine/rule/IQuickConsonantExecutor.h
//
// Quick-consonant subsystem output port. Implemented by TypingEngine (W7.4)
// so QuickStartConsonantRule (PreClassify) and QuickEndConsonantRule
// (PostClassify) can delegate quick-consonant handling.
//
// HandleQuickStartConsonant — covers 0a (f→ph, j→gi, w→qu at word start),
//                              0a-cont (undo on non-vowel follow-up), and
//                              0b (mid-word cc→ch family + uu→ươ). Returns
//                              Veto when the rule consumed the key (engine
//                              already processed it internally); Pass to
//                              let PushChar continue (covers 0a-cont
//                              fall-through and the no-match path).
// HandleQuickEndConsonant   — covers 2c (g→ng, h→nh, k→ch after vowel).
//                              Returns true on consumption; false to fall
//                              through to step 3 regular char.
#pragma once

#include "core/engine/rule/EngineRuleResult.h"

namespace NextKey::EngineRule {

class IQuickConsonantExecutor {
public:
    virtual ~IQuickConsonantExecutor() = default;

    [[nodiscard]] virtual Result HandleQuickStartConsonant(
        wchar_t keyChar, wchar_t lower, bool isUpper) = 0;

    [[nodiscard]] virtual bool HandleQuickEndConsonant(
        wchar_t keyChar, wchar_t lower, bool isUpper) = 0;
};

}  // namespace NextKey::EngineRule
