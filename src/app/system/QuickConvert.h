// VKey - Quick Convert (Hotkey-driven text conversion)
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "core/config/TypingConfig.h"
#include <string>
#include <vector>
#include <atomic>
#include <Windows.h>

namespace NextKey {

struct SelectionAnchor {
    DWORD start = 0;
    DWORD end = 0;
    bool valid = false;
    bool hasControl = false; // Whether the control supports EM_GETSEL
};

/// Quick-convert engine: copy → convert → paste → re-select → toast.
/// Runs headless in the main process, triggered by HookEngine hotkey callback.
class QuickConvert {
public:
    explicit QuickConvert(const ConvertConfig& config);
    void UpdateConfig(const ConvertConfig& config);

    /// Execute quick-convert (called from HookEngine on hotkey)
    void Execute();

private:
    // Clipboard operations
    [[nodiscard]] static bool OpenClipboardWithRetry(int maxRetries = 5, int intervalMs = 10) noexcept;
    [[nodiscard]] static std::wstring ReadClipboard();
    static bool WriteClipboard(const std::wstring& text);

    // Simulate Ctrl+C / Ctrl+V via SendInput
    static void SimulateCopy();
    static void SimulatePaste();

    // Wait for clipboard to have Unicode text
    bool WaitForClipboardUnicode(int maxWaitMs, int checkIntervalMs = 10);

    // Get exact cursor/selection position (EM_GETSEL for Edit/RichEdit)
    SelectionAnchor GetSelectionAnchor(HWND hwnd);

    // Re-select pasted text (EM_SETSEL or Shift+Left fallback)
    bool TryReselect(HWND hwnd, SelectionAnchor anchor, int pastedLength);

    // Simulate Shift+Left × length to select text backwards from cursor
    static void SimulateShiftLeftSelect(int length);

    // Apply a single conversion option to text
    [[nodiscard]] std::wstring ApplyConversion(const std::wstring& input, int optionIndex) const;

    // Get display name for a conversion option (for toast)
    [[nodiscard]] static const wchar_t* GetOptionName(int optionIndex);

    // Get list of enabled option indices
    [[nodiscard]] std::vector<int> GetEnabledOptions() const;

    // Sequential state
    struct SequentialState {
        std::wstring originText;       // Original text before any conversion
        SelectionAnchor anchor;        // Starting anchor of the selection
        HWND window = nullptr;         // Window where selection was made
        int currentIndex = 0;          // Current position in enabled options cycle
        DWORD lastConvertTime = 0;     // For timeout-based reset
        size_t contentHash = 0;        // Hash of converted text for change detection
        int lastPastedLength = 0;      // Length of last pasted text for recovery re-select
    };
    static constexpr DWORD SEQUENTIAL_TIMEOUT_MS = 5000;
    static constexpr int RESELECT_CUTOFF = 5000;

    [[nodiscard]] bool IsNewSelection(const std::wstring& clipText, HWND targetHwnd, const SelectionAnchor& anchor) const;
    [[nodiscard]] bool IsStillInCycle(HWND targetHwnd, const SelectionAnchor& anchor) const;
    void ResetSequentialState();

    ConvertConfig config_;
    SequentialState seqState_;
    std::atomic<bool> isRunning_{false};
};

}  // namespace NextKey
