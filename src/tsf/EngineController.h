// NexusKey - Engine Controller Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "stdafx.h"
#include "core/engine/IInputEngine.h"
#include "core/config/TypingConfig.h"
#include "core/config/ConfigEvent.h"
#include "core/ipc/SharedStateManager.h"
#include "CompositionManager.h"
#include "EditSession.h"
#include "LanguageBarButton.h"
#include <memory>
#include <msctf.h>

namespace NextKey {
namespace TSF {

/// Controller bridging TSF events and the Telex engine
class EngineController {
public:
    EngineController();
    ~EngineController();

    /// Set the TSF client ID for edit sessions
    void SetClientId(TfClientId clientId) { clientId_ = clientId; }

    /// Set the category manager for display attributes
    void SetCategoryMgr(ITfCategoryMgr* pCategoryMgr) { compositionMgr_.SetCategoryMgr(pCategoryMgr); }

    /// Check if we want to handle this key
    bool WantKey(UINT vkCode, bool isKeyDown);

    /// Handle a key press
    bool HandleKey(ITfContext* pContext, UINT vkCode);

    /// Process backspace
    void ProcessBackspace(ITfContext* pContext);

    /// Commit current composition
    void Commit(ITfContext* pContext);

    /// Commit with trailing character (e.g., space)
    void CommitWithChar(ITfContext* pContext, wchar_t appendChar);

    /// Reset engine state
    void Reset();

    /// Check if there's pending composition (for Ctrl shortcuts)
    bool HasComposition() const { return engine_->Count() > 0; }

    /// Check if TSF composition is active
    bool IsComposing() const { return compositionMgr_.IsComposing(); }

    /// Check if engine has buffer (for sync check)
    bool HasEngineBuffer() const { return engine_->Count() > 0; }

    /// Check if vkCode is a VNI/Combined digit key (1-9) that should NOT trigger commit
    bool IsVniDigitKey(UINT vkCode) const {
        return (config_.inputMethod == InputMethod::VNI ||
                config_.inputMethod == InputMethod::Combined) &&
               vkCode >= 0x31 && vkCode <= 0x39 &&
               !(GetKeyState(VK_SHIFT) & 0x8000);
    }

    /// Reset only engine buffer (for sync recovery)
    void ResetEngine() { engine_->Reset(); }

    /// Check for config changes (call periodically, e.g., on focus)
    /// Returns true if config was reloaded
    bool CheckConfigEvent();

    /// Check if engine is enabled (app is running)
    [[nodiscard]] bool IsEnabled() const noexcept { return engineEnabled_; }

    /// Check if Vietnamese mode is active
    [[nodiscard]] bool IsVietnameseMode() const noexcept { return vietnameseMode_; }

    /// Toggle Vietnamese/English mode (atomic flag + icon refresh)
    void ToggleVietnameseMode();

    /// Get current code table
    [[nodiscard]] CodeTable GetCodeTable() const noexcept { return config_.codeTable; }

    /// Set code table (updates config, no persistence yet)
    void SetCodeTable(CodeTable ct) noexcept { config_.codeTable = ct; }

    /// Initialize language bar button (call after SetClientId/SetCategoryMgr)
    bool InitLanguageBar(ITfThreadMgr* pThreadMgr);

    /// Cleanup language bar button
    void UninitLanguageBar();

    /// Re-read flags from SharedState (call on focus)
    void RefreshFlags();

    /// Non-owning access to the SharedStateManager — shared with ReadonlyContextProvider
    /// so both can read/write the same memory-mapped region without duplicating the
    /// mapping handle.
    [[nodiscard]] SharedStateManager* GetSharedStateManager() noexcept { return &sharedState_; }

    /// Check if context is blocked (password, PIN, etc.) and cache result.
    /// Call from OnTestKeyDown when context changes.
    void CheckContextBlocked(ITfContext* pContext);

    /// Whether current context blocks Vietnamese input
    [[nodiscard]] bool IsContextBlocked() const noexcept { return contextBlocked_; }

    /// Try to prepare a Backspace revive: sync-read preceding Vietnamese word
    /// before caret. On success caches word + range for HandleKey(VK_BACK) to consume.
    /// Returns true if a Vietnamese word was found — caller should eat the BS key.
    bool PrepareBackspaceRevive(ITfContext* pContext);

    /// Discard any pending revive state (word + range). Safe to call at any time.
    void ClearPendingRevive();

    /// Whether a revive is queued (set by PrepareBackspaceRevive, consumed by HandleKey).
    [[nodiscard]] bool HasPendingRevive() const noexcept { return pendingReviveRange_ != nullptr; }

private:
    void RequestEditSession(ITfContext* pContext, EditSession* pEditSession);

    /// Detect if current app is Scintilla-based (cached, updated on context change)
    void DetectScintillaApp();

    /// Apply config from SharedState
    void ApplySharedState(const SharedState& state);

    std::unique_ptr<IInputEngine> engine_;
    CompositionManager compositionMgr_;
    TypingConfig config_;
    InputMethod currentMethod_ = InputMethod::Telex;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    ConfigEvent configEvent_;       // For detecting config changes
    SharedStateManager sharedState_; // For reading config from App
    uint32_t lastEpoch_ = 0;        // Last seen config epoch
    bool engineEnabled_ = true;     // ENGINE_ENABLED flag from SharedState
    bool tsfActive_ = false;        // TSF_ACTIVE flag from SharedState (foreground app in TSF list)
    bool vietnameseMode_ = true;    // VIETNAMESE_MODE flag from SharedState
    bool abiOk_ = true;             // false → SharedState layout mismatch, disable TSF for this process
    LanguageBarButton* langBarButton_ = nullptr;  // Owned, Release'd in UninitLanguageBar
    ITfContext* lastContext_ = nullptr;   // Last seen context (AddRef'd for safe identity comparison)
    bool contextBlocked_ = false;        // True if current context blocks input (password, etc.)
    bool isScintillaApp_ = false;        // Cached: current app is Scintilla-based (Notepad++, etc.)

    // Pending Backspace revive — set by PrepareBackspaceRevive (called from OnTestKeyDown),
    // consumed by HandleKey(VK_BACK). CComPtr auto-manages ref count.
    std::wstring pendingReviveWord_;
    CComPtr<ITfRange> pendingReviveRange_;
};

}  // namespace TSF
}  // namespace NextKey
