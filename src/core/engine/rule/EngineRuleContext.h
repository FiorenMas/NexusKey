// src/core/engine/rule/EngineRuleContext.h
//
// Per-keystroke context handed to engine rules. Built once at PushChar entry,
// re-snapshotted at PostClassify boundary (action + freshly read state). POD-
// like; held by rules only for the duration of one Apply() call.
//
// Distinct from Pipeline::KeyContext because the engine sees a different
// surface — case-resolved chars + buffer/state refs + engine-internal flags —
// not VK codes + modifiers + ICompositionSession views.
//
// IMPORTANT — the `states` / `rawInput` / `config` fields are REFERENCES to
// live engine state, not snapshots. If a rule calls into an executor that
// mutates engine state (e.g. ProcessChar appends to states_), subsequent
// reads of ctx.states see the new value within the same Apply() call. Rules
// that need a stable view across an executor call must capture by value or
// re-read AFTER the call. Scalar fields (action / bias / escapeActive / ...)
// are by-value snapshots — they're rebuilt at the PostClassify boundary
// (see PushChar's `postCtx = ruleCtx; postCtx.action = action;` pattern).
#pragma once

#include <vector>

#include "core/engine/EnglishProtection.h"   // LanguageBias
#include "core/engine/TypingAction.h"        // TypingAction
#include "core/engine/TypingEngine.h"        // CharState
#include "core/config/TypingConfig.h"        // TypingConfig

namespace NextKey::EngineRule {

struct EngineRuleContext {
    wchar_t      keyChar;             // raw keystroke (case-preserved)
    wchar_t      lower;               // towlower(keyChar)
    bool         isUpper;             // iswupper(keyChar)
    TypingAction action;              // TypingAction::None in PreClassify; resolved in PostClassify
    bool         spellCheckDisabled;
    bool         allowEnglishBypass;
    bool         escapeActive;
    LanguageBias bias;
    bool         isVniDigitSeq;

    const std::vector<CharState>& states;
    const std::vector<wchar_t>&   rawInput;
    const TypingConfig&           config;
};

}  // namespace NextKey::EngineRule
