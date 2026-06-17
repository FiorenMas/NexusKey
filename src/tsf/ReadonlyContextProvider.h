// VKey - Readonly Context Provider Header
// SPDX-License-Identifier: AGPL-3.0-only
//
// When TSF_READONLY flag is set (foreground app uses Hook, not TSF full TIP),
// this class observes document edits and publishes a HookContextAnchor via
// SharedState so HookEngine can make context-aware decisions (auto-cap etc.)
// without consuming any keystrokes.
//
// Lifecycle: owned by TextService, advised in Activate(), unadvised in Deactivate().
// Gated on TSF_READONLY at per-event granularity — cheap when inactive.

#pragma once

#include "stdafx.h"
#include "core/ipc/SharedStateManager.h"

namespace NextKey {
namespace TSF {

class ReadonlyContextProvider : public ITfThreadMgrEventSink,
                                public ITfTextEditSink {
public:
    explicit ReadonlyContextProvider(SharedStateManager* pSharedState) noexcept;
    ~ReadonlyContextProvider();

    // Non-copyable, non-movable (owns COM sinks with cookie state).
    ReadonlyContextProvider(const ReadonlyContextProvider&) = delete;
    ReadonlyContextProvider& operator=(const ReadonlyContextProvider&) = delete;

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // ITfThreadMgrEventSink
    IFACEMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pDocMgr) override { (void)pDocMgr; return S_OK; }
    IFACEMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr) override { (void)pDocMgr; return S_OK; }
    IFACEMETHODIMP OnSetFocus(ITfDocumentMgr* pDocMgrFocus,
                              ITfDocumentMgr* pDocMgrPrevFocus) override;
    IFACEMETHODIMP OnPushContext(ITfContext* pContext) override { (void)pContext; return S_OK; }
    IFACEMETHODIMP OnPopContext(ITfContext* pContext) override { (void)pContext; return S_OK; }

    // ITfTextEditSink
    IFACEMETHODIMP OnEndEdit(ITfContext* pContext, TfEditCookie ecReadOnly,
                             ITfEditRecord* pEditRecord) override;

    /// Advise ITfThreadMgrEventSink and prime with current focus. Idempotent.
    /// Returns true on success.
    [[nodiscard]] bool Advise(ITfThreadMgr* pThreadMgr, TfClientId clientId);

    /// Unadvise all sinks. Safe to call multiple times.
    void Unadvise();

private:
    /// Advise ITfTextEditSink on the top context of the given doc mgr.
    /// Unadvises any previously-advised context first.
    void AdviseEditSink(ITfDocumentMgr* pDocMgr);

    /// Unadvise the current ITfTextEditSink (if any).
    void UnadviseEditSink();

    /// Read preceding chars via the provided edit cookie, derive flags,
    /// push anchor to SharedState. Called from OnEndEdit.
    void UpdateAnchor(ITfContext* pContext, TfEditCookie ec);

    /// Quick flag check — are we in readonly mode right now?
    [[nodiscard]] bool IsReadonlyModeActive() const noexcept;

    SharedStateManager* pSharedState_;        // non-owning, shared with EngineController
    ITfThreadMgr*       pThreadMgr_ = nullptr;  // not owned
    DWORD               threadMgrCookie_ = TF_INVALID_COOKIE;

    CComPtr<ITfContext> pAdvisedContext_;
    DWORD               editCookie_ = TF_INVALID_COOKIE;

    /// Last anchor payload handed to SharedState. Used to skip no-op WriteAnchor
    /// calls so readers in another process don't retry seqlocks for nothing every
    /// time the user types inside a single word.
    HookContextAnchor   lastWritten_{};

    LONG                refCount_   = 1;
    bool                isFocused_  = false;  // this thread's doc mgr currently focused
};

}  // namespace TSF
}  // namespace NextKey
