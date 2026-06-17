// VKey - Composition Manager
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "stdafx.h"

namespace NextKey {
namespace TSF {

class EngineController;

/// Manages TSF composition lifecycle and receives composition events
class CompositionManager : public ITfCompositionSink {
public:
    CompositionManager() = default;
    ~CompositionManager() { TerminateComposition(); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refCount_); }
    // Intentionally embedded COM object — lifetime managed by EngineController, not COM ref counting
    IFACEMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&refCount_); }

    // ITfCompositionSink
    IFACEMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition* pComposition) override;

    /// Start a new composition
    bool StartComposition(ITfContext* pContext, TfEditCookie ec, ITfComposition** ppComposition);

    /// Set text in current composition
    bool SetCompositionText(TfEditCookie ec, const std::wstring& text);

    /// End composition (commits text)
    void EndComposition(TfEditCookie ec);

    /// Terminate composition without commit
    void TerminateComposition();

    /// Check if composition is active
    bool IsComposing() const { return pComposition_ != nullptr; }

    /// Set the category manager for display attributes
    void SetCategoryMgr(ITfCategoryMgr* pCategoryMgr) { pCategoryMgr_ = pCategoryMgr; }

    /// Set engine controller for state synchronization
    void SetEngineController(EngineController* pEngineController) { pEngineController_ = pEngineController; }

private:
    /// Apply invisible display attribute to range
    void ApplyDisplayAttribute(TfEditCookie ec, ITfRange* pRange);

    /// Clear display attribute from range (on end composition)
    void ClearDisplayAttribute(TfEditCookie ec, ITfRange* pRange);

    /// Move caret to end of composition range
    void MoveCaretToEnd(TfEditCookie ec);

    ITfComposition* pComposition_ = nullptr;
    ITfContext* pContext_ = nullptr;  // Current context
    ITfCategoryMgr* pCategoryMgr_ = nullptr;  // Not owned, don't release
    EngineController* pEngineController_ = nullptr; // Not owned, don't release
    TfGuidAtom gaDisplayAttribute_ = TF_INVALID_GUIDATOM;
    ULONG refCount_ = 1;
};

}  // namespace TSF
}  // namespace NextKey
