// src/core/engine/rule/EngineRuleResult.h
//
// Result of an engine-rule Apply() call.
//   Pass    — rule did not act; registry continues to next rule in phase.
//   Handled — rule acted; registry stops this phase but next phase still runs.
//   Veto    — rule acted; caller (TypingEngine::PushChar) returns immediately.
// Differs from Pipeline::Result: there is no cross-stage semantics here —
// "all later work" means "the rest of PushChar's body".
#pragma once

namespace NextKey::EngineRule {

enum class Result : unsigned char {
    Pass    = 0,
    Handled = 1,
    Veto    = 2,
};

}  // namespace NextKey::EngineRule
