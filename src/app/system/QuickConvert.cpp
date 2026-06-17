// VKey - Quick Convert Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "QuickConvert.h"
#include "ToastPopup.h"
#include "HookEngine.h"
#include "helpers/AppHelpers.h"
#include "core/engine/CodeTableConverter.h"
#include "core/Debug.h"
#include "core/CrashLog.h"
#include <exception>
#include <functional>
#include <thread>

#include <cstdio>
#include <cstdarg>

void QuickConvertLogToFile(const wchar_t* format, ...) {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        std::wstring logPath(exePath);
        size_t lastSlash = logPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            logPath = logPath.substr(0, lastSlash + 1) + L"vkey_quick_convert.log";
            
            FILE* file = nullptr;
            if (_wfopen_s(&file, logPath.c_str(), L"a, ccs=UTF-8") == 0 && file) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fwprintf(file, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] QC: ", 
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            
            va_list args;
            va_start(args, format);
            vfwprintf(file, format, args);
            va_end(args);
            
            fwprintf(file, L"\n");
            fclose(file);
        }
    }
    }
}

#define QC_LOG(fmt, ...) \
    do { \
        if (config_.enableLog) { \
            QuickConvertLogToFile(fmt, ##__VA_ARGS__); \
        } \
    } while(0)

namespace NextKey {

// Conversion option indices (matching plan)
enum ConvertOption : int {
    kEncoding = 0,
    kToUpper = 1,
    kToLower = 2,
    kCapsFirst = 3,
    kCapsEach = 4,
    kRemoveDiacritics = 5
};

QuickConvert::QuickConvert(const ConvertConfig& config)
    : config_(config) {
}

void QuickConvert::UpdateConfig(const ConvertConfig& config) {
    config_ = config;
    ResetSequentialState();
}

// ═══════════════════════════════════════════════════════════
// Execute flow
// ═══════════════════════════════════════════════════════════

void QuickConvert::Execute() {
    QC_LOG(L"--- User pressed hotkey ---");

    // Prevent clipboard corruption if the user somehow triggers multiple overlapping events
    bool expected = false;
    if (!isRunning_.compare_exchange_strong(expected, true)) {
        QC_LOG(L"QuickConvert already running, ignoring overlapped execution");
        return;
    }

    // Run in a separate detached thread to avoid blocking the global Windows keyboard hook
    std::thread([this]() {
        struct AutoReset {
            std::atomic<bool>& flag;
            ~AutoReset() { flag = false; }
        } resetter{isRunning_};

        try {
        QC_LOG(L"=== QuickConvert::Execute started ===");

        // 1a. Capture state IMMEDIATELY before waiting for modifiers 
        // We must fetch the anchor while the target window still firmly holds the selection
        HWND currentWindow = GetForegroundWindow();
        SelectionAnchor anchor = GetSelectionAnchor(currentWindow);
        QC_LOG(L"Anchor valid: %d, start: %u, end: %u", anchor.valid, anchor.start, anchor.end);

        // 2. Save current clipboard content
        std::wstring savedClipboard = ReadClipboard();

        // Track sequence after each clipboard mutation we perform, so the
        // bail-restore paths below can detect a third-party write (user
        // Ctrl+C, clipboard manager) between OUR last touch and the restore.
        DWORD ourLastSeq = GetClipboardSequenceNumber();
        auto restoreSavedClipboardIfSafe = [&]() {
            if (savedClipboard.empty()) return;
            if (GetClipboardSequenceNumber() != ourLastSeq) {
                QC_LOG(L"Skip restore: clipboard touched by 3rd party since our last write");
                return;
            }
            WriteClipboard(savedClipboard);
            // No post-write seq update: every caller returns immediately.
        };

        std::wstring clipText;
        bool gotClip = false;

        // If the control supports EM_GETSEL and has no active selection, we skip SimulateCopy
        bool skipCopy = (anchor.hasControl && !anchor.valid);

        if (!skipCopy) {
            // Try copy up to 2 times with backoff
            for (int attempt = 0; attempt < 2; ++attempt) {
                if (OpenClipboardWithRetry()) {
                    EmptyClipboard();
                    CloseClipboard();
                    ourLastSeq = GetClipboardSequenceNumber();
                }

                SimulateCopy();

                if (WaitForClipboardUnicode(150)) {
                    clipText = ReadClipboard();
                    if (!clipText.empty()) {
                        gotClip = true;
                        // SimulateCopy's Ctrl+C produced a clipboard write; treat
                        // that as part of our action sequence.
                        ourLastSeq = GetClipboardSequenceNumber();
                        break;
                    }
                }

                QC_LOG(L"Copy attempt %d failed, retrying...", attempt + 1);
                Sleep(30 * (attempt + 1));  // 30ms -> 60ms backoff
            }
        }

        QC_LOG(L"Copied text length: %zu", clipText.size());

        // 5. Sequential recovery: if clipboard is empty but we're mid-cycle,
        // re-select failed last time — recover by re-selecting the previous paste
        bool recoveryMode = false;
        if (!gotClip || clipText.empty()) {
            if (config_.sequential && config_.autoPaste && IsStillInCycle(currentWindow, anchor)) {
                QC_LOG(L"Sequential recovery: re-selecting previous paste (length: %d)", seqState_.lastPastedLength);
                SimulateShiftLeftSelect(seqState_.lastPastedLength);
                Sleep(30);
                recoveryMode = true;
            } else if (!savedClipboard.empty()) {
                QC_LOG(L"No text copied from selection. Falling back to converting clipboard content directly.");
                clipText = savedClipboard;
                gotClip = true;
            } else {
                QC_LOG(L"Clipboard empty, no text copied.");
                restoreSavedClipboardIfSafe();
                return;
            }
        }

        // 6. Determine enabled options
        auto enabledOptions = GetEnabledOptions();
        if (enabledOptions.empty()) {
            QC_LOG(L"No conversions enabled");
            // No conversions enabled — restore and bail
            restoreSavedClipboardIfSafe();
            return;
        }

    std::wstring result;
    const wchar_t* toastMsg = nullptr;  // Which conversion was applied

    if (config_.sequential && config_.autoPaste) {
        // Sequential mode: cycle through enabled options on repeated presses
        DWORD now = GetTickCount();

        if (!recoveryMode && IsNewSelection(clipText, currentWindow, anchor)) {
            // New selection: start fresh cycle
            QC_LOG(L"Starting new sequential cycle");
            seqState_.originText = clipText;
            seqState_.anchor = anchor;
            seqState_.window = currentWindow;
            seqState_.currentIndex = 0;
            seqState_.contentHash = std::hash<std::wstring>{}(clipText);
        } else {
            // Same selection: advance to next option
            QC_LOG(L"Continuing sequential cycle");
            seqState_.currentIndex++;
            if (seqState_.currentIndex >= static_cast<int>(enabledOptions.size())) {
                // Wrap back to original text
                seqState_.currentIndex = -1;  // -1 = show original
            }
        }

        seqState_.lastConvertTime = now;

        if (seqState_.currentIndex < 0) {
            // Show original text
            result = seqState_.originText;
            toastMsg = L"\x2192 G\x1ED1" L"c";  // → Gốc
            QC_LOG(L"Restoring original text");
        } else {
            int optIdx = enabledOptions[seqState_.currentIndex];
            result = ApplyConversion(seqState_.originText, optIdx);
            toastMsg = GetOptionName(optIdx);
            QC_LOG(L"Applied conversion option %d", optIdx);
        }

        // Update hash for next comparison
        seqState_.contentHash = std::hash<std::wstring>{}(result);
    } else {
        // Non-sequential mode: apply all enabled conversions at once
        result = clipText;

        // Encoding conversion first (if source != dest)
        if (config_.sourceEncoding != config_.destEncoding) {
            auto srcTable = static_cast<CodeTable>(config_.sourceEncoding);
            auto dstTable = static_cast<CodeTable>(config_.destEncoding);
            std::wstring unicode = CodeTableConverter::DecodeString(result, srcTable);
            result = CodeTableConverter::EncodeString(unicode, dstTable);
        }

        // Text transformations (mutually exclusive case options, removeMark is independent)
        if (config_.removeMark) {
            result = CodeTableConverter::RemoveDiacritics(result);
            toastMsg = GetOptionName(kRemoveDiacritics);
        }
        if (config_.allCaps) {
            result = CodeTableConverter::ToUpper(result);
            toastMsg = GetOptionName(kToUpper);
        } else if (config_.allLower) {
            result = CodeTableConverter::ToLower(result);
            toastMsg = GetOptionName(kToLower);
        } else if (config_.capsFirst) {
            result = CodeTableConverter::ToSentenceCase(result);
            toastMsg = GetOptionName(kCapsFirst);
        } else if (config_.capsEach) {
            result = CodeTableConverter::ToTitleCase(result);
            toastMsg = GetOptionName(kCapsEach);
        }

        // If only encoding conversion, show encoding toast
        if (!toastMsg && config_.sourceEncoding != config_.destEncoding) {
            toastMsg = GetOptionName(kEncoding);
        }
    }

    // 7. Check if anything changed
    if (result == clipText) {
        QC_LOG(L"Result same as clip text, no action needed");
        // No change — restore original clipboard
        restoreSavedClipboardIfSafe();
        return;
    }

    if (config_.autoPaste) {
        QC_LOG(L"Pasting converted text (length: %zu)", result.size());
        // 8a. Write converted text to clipboard and paste it
        WriteClipboard(result);

        SimulatePaste();

        // Wait for paste operation to finish (Electron apps need 50-200ms)
        Sleep(100);

        // 9. Re-select pasted text
        // In recovery mode, use stored anchor (current anchor is invalid since there was no selection)
        SelectionAnchor reselectAnchor = recoveryMode ? seqState_.anchor : anchor;
        QC_LOG(L"Reselecting pasted text (recovery: %d)", recoveryMode);
        TryReselect(currentWindow, reselectAnchor, static_cast<int>(result.size()));

        // Track pasted length for sequential recovery on next press
        if (config_.sequential) {
            seqState_.lastPastedLength = static_cast<int>(result.size());
        }

        // Don't restore clipboard — user expects converted text to stay
    } else {
        QC_LOG(L"Writing to clipboard only");
        // 8b. Just write to clipboard (no paste)
        WriteClipboard(result);
    }

    // 10. Toast notification — show which conversion was applied
    if (config_.alertDone && toastMsg) {
        std::wstring msg(toastMsg);
        std::thread([msg]() {
            try {
                ToastPopup::Show(msg, 1500);
            } catch (const std::exception& e) {
                NextKey::CrashLog(L"QuickConvert::ToastThread", e.what());
            } catch (...) {
                NextKey::CrashLog(L"QuickConvert::ToastThread", "(non-std exception)");
            }
        }).detach();
    }
    } catch (const std::exception& e) {
        NextKey::CrashLog(L"QuickConvert::Execute::thread", e.what());
    } catch (...) {
        NextKey::CrashLog(L"QuickConvert::Execute::thread", "(non-std exception)");
    }
    }).detach();
}

bool QuickConvert::OpenClipboardWithRetry(int maxRetries, int intervalMs) noexcept {
    for (int i = 0; i < maxRetries; ++i) {
        if (OpenClipboard(nullptr)) return true;
        Sleep(intervalMs);
    }
    return false;
}

std::wstring QuickConvert::ReadClipboard() {
    if (!OpenClipboardWithRetry()) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return L"";
    }
    auto* pText = static_cast<const wchar_t*>(GlobalLock(hData));
    if (!pText) {
        CloseClipboard();
        return L"";
    }
    std::wstring text(pText);
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

bool QuickConvert::WriteClipboard(const std::wstring& text) {
    if (!OpenClipboardWithRetry()) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    auto* pDst = static_cast<wchar_t*>(GlobalLock(hMem));
    if (!pDst) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    memcpy(pDst, text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

// ═══════════════════════════════════════════════════════════
// Key simulation
// ═══════════════════════════════════════════════════════════

void QuickConvert::SimulateCopy() {
    INPUT inputs[9] = {};
    int n = 0;
    
    // Force-release ALL modifiers (unconditional, harmless if not held)
    for (WORD vk : {static_cast<WORD>(VK_SHIFT), static_cast<WORD>(VK_MENU), static_cast<WORD>(VK_CONTROL), static_cast<WORD>(VK_LWIN), static_cast<WORD>(VK_RWIN)}) {
        inputs[n].type = INPUT_KEYBOARD;
        inputs[n].ki.wVk = vk;
        inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
        n++;
    }

    // Ctrl+C (Ctrl down, C down, C up, Ctrl up)
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = VK_CONTROL;
    inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    n++;
    
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = 'C';
    inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    n++;

    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = 'C';
    inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    n++;

    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = VK_CONTROL;
    inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    n++;

    SendInput(n, inputs, sizeof(INPUT));
}

void QuickConvert::SimulatePaste() {
    INPUT inputs[9] = {};
    int n = 0;

    // Force-release ALL modifiers
    for (WORD vk : {static_cast<WORD>(VK_SHIFT), static_cast<WORD>(VK_MENU), static_cast<WORD>(VK_CONTROL), static_cast<WORD>(VK_LWIN), static_cast<WORD>(VK_RWIN)}) {
        inputs[n].type = INPUT_KEYBOARD;
        inputs[n].ki.wVk = vk;
        inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
        n++;
    }

    // Ctrl+V (Ctrl down, V down, V up, Ctrl up)
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = VK_CONTROL;
    inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    n++;

    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = 'V';
    inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    n++;

    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = 'V';
    inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    n++;

    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = VK_CONTROL;
    inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[n].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
    n++;

    SendInput(n, inputs, sizeof(INPUT));
}


bool QuickConvert::WaitForClipboardUnicode(int maxWaitMs, int checkIntervalMs) {
    if (maxWaitMs <= 0) return false;
    
    int elapsedMs = 0;
    while (elapsedMs < maxWaitMs) {
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            return true;
        }
        Sleep(checkIntervalMs);
        elapsedMs += checkIntervalMs;
    }
    return false;
}

SelectionAnchor QuickConvert::GetSelectionAnchor(HWND hwnd) {
    SelectionAnchor anchor;
    if (!hwnd) return anchor;

    HWND targetCtrl = NextKey::GetFocusedChildOrForeground(hwnd);
    if (!targetCtrl) return anchor;

    DWORD start = 0, end = 0;
    DWORD_PTR dummy = 0;
    // CRITICAL: Use SendMessageTimeoutW to prevent 3-second hangs if targetCtrl is unresponsive
    LRESULT lResult = SendMessageTimeoutW(targetCtrl, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end), SMTO_ABORTIFHUNG | SMTO_NORMAL, 50, &dummy);
    
    if (lResult != 0) {
        anchor.hasControl = true;
        // If end > start, the control successfully returned a selection range
        if (end > start) {
            anchor.start = start;
            anchor.end = end;
            anchor.valid = true;
        }
    }
    return anchor;
}

bool QuickConvert::TryReselect(HWND hwnd, SelectionAnchor anchor, int pastedLength) {
    if (pastedLength <= 0) return true;

    // Check if should skip EM_SETSEL (RichEdit controls can behave badly with raw EM_SETSEL if active, but we'll try)
    bool skipEmSetsel = false;
    HWND targetCtrl = NextKey::GetFocusedChildOrForeground(hwnd);
    if (targetCtrl) {
        wchar_t className[64] = {0};
        GetClassNameW(targetCtrl, className, 64);
        skipEmSetsel = (wcsstr(className, L"RichEdit") != nullptr ||
                        wcsstr(className, L"RICHEDIT") != nullptr ||
                        wcsstr(className, L"_WwG") != nullptr);
    }

    if (anchor.valid && targetCtrl && !skipEmSetsel) {
        // Poll: wait for selection to collapse (paste committed)
        // Max 300ms (12 × 25ms)
        for (int i = 0; i < 12; i++) {
            Sleep(25);

            DWORD s = 0, e = 0;
            DWORD_PTR dummy = 0;
            SendMessageTimeoutW(targetCtrl, EM_GETSEL, reinterpret_cast<WPARAM>(&s), reinterpret_cast<LPARAM>(&e), SMTO_ABORTIFHUNG | SMTO_NORMAL, 50, &dummy);

            if (s == e) {  // Selection collapsed → paste done!
                // Use actual caret position (e) which accounts for Unicode normalization
                if (e >= anchor.start) {
                    SendMessageTimeoutW(targetCtrl, EM_SETSEL, anchor.start, e, SMTO_ABORTIFHUNG | SMTO_NORMAL, 50, &dummy);
                    QC_LOG(L"Reselected using EM_SETSEL dynamic caret (%u to %u)", anchor.start, e);
                    return true;
                }
                // Fallback using calculated length
                DWORD fallbackEnd = anchor.start + pastedLength;
                SendMessageTimeoutW(targetCtrl, EM_SETSEL, anchor.start, fallbackEnd, SMTO_ABORTIFHUNG | SMTO_NORMAL, 50, &dummy);
                QC_LOG(L"Reselected using EM_SETSEL fallback length (%u to %u)", anchor.start, fallbackEnd);
                return true;
            }
        }

        QC_LOG(L"Tier 1 (EM_SETSEL) failed - paste did not settle in time");
        // If EM_SETSEL was supposed to work but timed out, do NOT fall through to Shift+Left
        // because the app might still be processing the paste and keystrokes would corrupt it.
        return false;
    }

    // TIER 2: Generic apps using Keystrokes (Shift + Left × N)
    QC_LOG(L"Reselecting using Shift+Left fallback (length: %d)", pastedLength);
    SimulateShiftLeftSelect(pastedLength);
    return true;
}

void QuickConvert::SimulateShiftLeftSelect(int length) {
    if (length <= 0) return;

    // Safety limit to prevent locking up the OS
    if (length > RESELECT_CUTOFF) length = RESELECT_CUTOFF;

    size_t charCount = static_cast<size_t>(length);
    constexpr size_t BATCH = 32;

    for (size_t sent = 0; sent < charCount; ) {
        size_t batchSize = (charCount - sent > BATCH) ? BATCH : (charCount - sent);
        size_t inputCount = 2 + batchSize * 2;  // Shift down + (Left down + Left up) * N + Shift up
        std::vector<INPUT> inputs(inputCount, INPUT{});

        // Shift down
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_SHIFT;
        inputs[0].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;

        for (size_t i = 0; i < batchSize; ++i) {
            size_t base = 1 + i * 2;
            // Left down
            inputs[base].type = INPUT_KEYBOARD;
            inputs[base].ki.wVk = VK_LEFT;
            inputs[base].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
            // Left up
            inputs[base + 1].type = INPUT_KEYBOARD;
            inputs[base + 1].ki.wVk = VK_LEFT;
            inputs[base + 1].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[base + 1].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;
        }

        // Shift up
        inputs[inputCount - 1].type = INPUT_KEYBOARD;
        inputs[inputCount - 1].ki.wVk = VK_SHIFT;
        inputs[inputCount - 1].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[inputCount - 1].ki.dwExtraInfo = HookEngine::VKEY_EXTRA_INFO;

        SendInput(static_cast<UINT>(inputCount), inputs.data(), sizeof(INPUT));
        sent += batchSize;

        if (sent < charCount) {
            Sleep(5);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Conversion logic
// ═══════════════════════════════════════════════════════════

std::wstring QuickConvert::ApplyConversion(const std::wstring& input, int optionIndex) const {
    switch (optionIndex) {
        case kEncoding: {
            auto srcTable = static_cast<CodeTable>(config_.sourceEncoding);
            auto dstTable = static_cast<CodeTable>(config_.destEncoding);
            std::wstring unicode = CodeTableConverter::DecodeString(input, srcTable);
            return CodeTableConverter::EncodeString(unicode, dstTable);
        }
        case kToUpper:
            return CodeTableConverter::ToUpper(input);
        case kToLower:
            return CodeTableConverter::ToLower(input);
        case kCapsFirst:
            return CodeTableConverter::ToSentenceCase(input);
        case kCapsEach:
            return CodeTableConverter::ToTitleCase(input);
        case kRemoveDiacritics:
            return CodeTableConverter::RemoveDiacritics(input);
        default:
            return input;
    }
}

const wchar_t* QuickConvert::GetOptionName(int optionIndex) {
    switch (optionIndex) {
        case kEncoding:        return L"\x2192 Chuy\x1EC3n m\x00E3";              // → Chuyển mã
        case kToUpper:         return L"\x2192 ch\x1EEF HOA";                     // → chữ HOA
        case kToLower:         return L"\x2192 ch\x1EEF th\x01B0\x1EDD" L"ng";   // → chữ thường
        case kCapsFirst:       return L"\x2192 Hoa \x0111\x1EA7u c\x00E2u";      // → Hoa đầu câu
        case kCapsEach:        return L"\x2192 Hoa T\x1EEB" L"ng Ch\x1EEF";      // → Hoa Từng Chữ
        case kRemoveDiacritics:return L"\x2192 B\x1ECF d\x1EA5u";                // → Bỏ dấu
        default:               return L"\x2192 Chuy\x1EC3" L"n m\x00E3 xong";    // → Chuyển mã xong
    }
}

std::vector<int> QuickConvert::GetEnabledOptions() const {
    std::vector<int> options;

    if (config_.sourceEncoding != config_.destEncoding) {
        options.push_back(kEncoding);
    }
    if (config_.allCaps) {
        options.push_back(kToUpper);
    }
    if (config_.allLower) {
        options.push_back(kToLower);
    }
    if (config_.capsFirst) {
        options.push_back(kCapsFirst);
    }
    if (config_.capsEach) {
        options.push_back(kCapsEach);
    }
    if (config_.removeMark) {
        options.push_back(kRemoveDiacritics);
    }

    return options;
}

// ═══════════════════════════════════════════════════════════
// Sequential state management
// ═══════════════════════════════════════════════════════════

bool QuickConvert::IsNewSelection(const std::wstring& clipText, HWND targetHwnd, const SelectionAnchor& anchor) const {
    DWORD now = GetTickCount();

    if ((now - seqState_.lastConvertTime) > SEQUENTIAL_TIMEOUT_MS) {
        QC_LOG(L"IsNewSelection: Timeout");
        return true;
    }

    if (targetHwnd != seqState_.window) {
        QC_LOG(L"IsNewSelection: Different window");
        return true;
    }
    
    if (anchor.valid && seqState_.anchor.valid) {
        if (anchor.start != seqState_.anchor.start) {
            QC_LOG(L"IsNewSelection: Different selection anchor");
            return true;
        }
    }

    if (clipText == seqState_.originText) {
        QC_LOG(L"IsNewSelection: Matched origin text");
        return false;
    }
    
    size_t clipHash = std::hash<std::wstring>{}(clipText);
    if (clipHash == seqState_.contentHash) {
        QC_LOG(L"IsNewSelection: Matched last output");
        return false;
    }

    QC_LOG(L"IsNewSelection: Content changed");
    return true;
}

bool QuickConvert::IsStillInCycle(HWND targetHwnd, const SelectionAnchor& anchor) const {
    // No previous conversion
    if (seqState_.lastConvertTime == 0 || seqState_.lastPastedLength <= 0) {
        return false;
    }

    // Timeout check
    DWORD now = GetTickCount();
    if ((now - seqState_.lastConvertTime) > SEQUENTIAL_TIMEOUT_MS) {
        QC_LOG(L"IsStillInCycle: Timeout");
        return false;
    }

    // Window check
    if (targetHwnd != seqState_.window) {
        QC_LOG(L"IsStillInCycle: Different window");
        return false;
    }

    // Anchor validation (Edit controls only): verify cursor is at expected position
    // If user moved cursor, recovery would select wrong text — abort
    if (anchor.valid && seqState_.anchor.valid) {
        DWORD expectedCursor = seqState_.anchor.start + static_cast<DWORD>(seqState_.lastPastedLength);
        if (anchor.start != expectedCursor || anchor.end != expectedCursor) {
            QC_LOG(L"IsStillInCycle: Cursor moved (expected %u, got %u-%u)", expectedCursor, anchor.start, anchor.end);
            return false;
        }
    }

    QC_LOG(L"IsStillInCycle: Yes (lastPastedLength: %d)", seqState_.lastPastedLength);
    return true;
}

void QuickConvert::ResetSequentialState() {
    seqState_ = SequentialState{};
}

}  // namespace NextKey
