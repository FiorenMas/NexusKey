// VKey - TSF Language Bar Button
// SPDX-License-Identifier: AGPL-3.0-only
//
// Implements ITfLangBarItemButton to show V/E toggle icon in the system tray.
// Windows automatically shows the button when the TIP is active and hides it
// when the user switches to another keyboard layout. This replaces the
// unreliable cross-process EXE tray icon approach.

#pragma once

#include "stdafx.h"

namespace NextKey {
namespace TSF {

class EngineController;

/// TSF Language Bar button — shown in system tray when VKey TIP is active.
/// Left-click toggles Vietnamese/English mode.
/// Right-click shows context menu (Settings, About).
class LanguageBarButton : public ITfLangBarItemButton, public ITfSource {
public:
    LanguageBarButton();
    ~LanguageBarButton();

    // Non-copyable
    LanguageBarButton(const LanguageBarButton&) = delete;
    LanguageBarButton& operator=(const LanguageBarButton&) = delete;

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    IFACEMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    IFACEMETHODIMP GetStatus(DWORD* pdwStatus) override;
    IFACEMETHODIMP Show(BOOL fShow) override;
    IFACEMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;

    // ITfLangBarItemButton
    IFACEMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    IFACEMETHODIMP InitMenu(ITfMenu* pMenu) override;
    IFACEMETHODIMP OnMenuSelect(UINT wID) override;
    IFACEMETHODIMP GetIcon(HICON* phIcon) override;
    IFACEMETHODIMP GetText(BSTR* pbstrText) override;

    // ITfSource
    IFACEMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    IFACEMETHODIMP UnadviseSink(DWORD dwCookie) override;

    /// Initialize and add to language bar
    bool Initialize(EngineController* controller, ITfLangBarItemMgr* langBarItemMgr);

    /// Remove from language bar and cleanup
    void Uninitialize();

    /// Notify TSF to re-query icon/text/tooltip (call after V/E mode change)
    void Refresh();

private:
    ULONG refCount_ = 1;
    EngineController* controller_ = nullptr;
    ITfLangBarItemMgr* langBarItemMgr_ = nullptr;
    ITfLangBarItemSink* itemSink_ = nullptr;
};

}  // namespace TSF
}  // namespace NextKey
