// src/core/engine/rule/IModifierSubExecutor.h
//
// Modifier SUB-handler output port. Implemented by TypingEngine so each
// ModifierProposal can delegate to the engine's existing per-action body
// without lifting state mutation out of TypingEngine.
//
// Wave 8.1 introduced this port with a single method (HandleAdjacentCircumflex).
// W8.2 added HornW, W8.3 StrokeD, W8.4 HornInsert (bracket keys), W8.5
// VniCircumflex / VniHorn / VniBreve. See docs/plans/2026-05-25-feature-pipeline-w8-retro.md.
//
// Distinct from IModifierExecutor (W7.3): that port owns the OUTER dispatch
// (HandleModifierAction → ProcessModifier switch); this port owns INNER
// sub-handlers reached by ProcessModifier's per-action cases.
#pragma once

#include "core/engine/TypingAction.h"

namespace NextKey::EngineRule {

class IModifierSubExecutor {
public:
    virtual ~IModifierSubExecutor() = default;

    // CircumflexA/E/O routed from ProcessModifier. Body lives on TypingEngine
    // (private virtual override) — wrap-don't-lift per W7 retro AD-1.
    [[nodiscard]] virtual bool HandleAdjacentCircumflex(TypingAction action,
                                                        wchar_t keyChar) = 0;

    // HornW (Telex `w` modifier, P1-P8). Body lives on TypingEngine; this
    // port lets HornModifierProposal route dispatch through the proposal
    // layer (W8.2).
    [[nodiscard]] virtual bool HandleHornW(TypingAction action,
                                            wchar_t keyChar) = 0;

    // StrokeD (Telex `dd` / VNI `d9` → đ). Body lives on TypingEngine;
    // this port lets StrokeDProposal route dispatch through the proposal
    // layer (W8.3). HandleVniStroke is a thin forwarder; routing the
    // StrokeD dispatch case through the proposal indirectly covers it.
    [[nodiscard]] virtual bool HandleStrokeD(TypingAction action,
                                              wchar_t keyChar) = 0;

    // HornInsert (Telex `[` / `]` → ơ / ư direct insertion). One handler
    // covers both HornInsertO and HornInsertU via the `action` parameter.
    // W8.4 port for BracketProposal.
    [[nodiscard]] virtual bool HandleHornInsert(TypingAction action,
                                                  wchar_t keyChar) = 0;

    // VniCircumflex (VNI `6` → â/ê/ô). W8.5 port for VniCircumflexProposal.
    // Body forwards to ProcessVniVowelModifier(Circumflex, key).
    [[nodiscard]] virtual bool HandleVniCircumflex(TypingAction action,
                                                     wchar_t keyChar) = 0;

    // VniHorn (VNI `7` → ơ/ư). W8.5 port for VniHornProposal. Body
    // implements priority order similar to HandleHornW.
    [[nodiscard]] virtual bool HandleVniHorn(TypingAction action,
                                              wchar_t keyChar) = 0;

    // VniBreve (VNI `8` → ă). W8.5 port for VniBreveProposal. Body forwards
    // to ProcessVniVowelModifier(Breve, key).
    [[nodiscard]] virtual bool HandleVniBreve(TypingAction action,
                                                wchar_t keyChar) = 0;
};

}  // namespace NextKey::EngineRule
