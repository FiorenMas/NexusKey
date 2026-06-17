// src/app/output/SplitDispatchInjector.cpp
//
// D3 implementation. Split SendInput with Sleep between BS batch and
// char batch. Covers Electron (Discord/Slack/VSCode, sleepMs=6) and
// Console (CMD/PowerShell, sleepMs=5). Distinct from Win32 batched
// path because these renderers drop the second half of a single big
// batch under load — splitting + brief Sleep gives the message loop a
// chance to drain before the second wave.
//
// Spec: docs/plans/sprint-2-output-injector.md §2.4
#include "SplitDispatchInjector.h"

#include <array>

#include "Internal.h"

namespace NextKey::Output {

namespace {

constexpr std::size_t kMaxBatch = 256;

// Helpers duplicated from Win32SendInputInjector.cpp. D4 cleanup may
// extract into Internal.h if profiling shows the factor-out is worth
// the cross-TU dependency. Kept intentionally local for now.
INPUT MakeKey(WORD vk, bool keyup) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = keyup ? KEYEVENTF_KEYUP : 0u;
    in.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
    return in;
}

INPUT MakeUnicodeChar(WCHAR ch, bool keyup) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = static_cast<WORD>(ch);
    in.ki.dwFlags = KEYEVENTF_UNICODE | (keyup ? KEYEVENTF_KEYUP : 0u);
    in.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
    return in;
}

}  // namespace

bool SplitDispatchInjector::Replace(std::size_t bsCount,
                                    std::wstring_view text) noexcept {
    // Batch 1: bait char (Chromium suggest-dismiss, when applicable) +
    // backspaces. Predicate shared with Win32SendInputInjector via
    // Internal::ShouldEmitBait — WebView2 / Electron-on-Chromium hosts
    // inherit the same selection-eat quirk as Edge's omnibox.
    std::array<INPUT, kMaxBatch> bsBuf{};
    std::size_t bi = 0;
    const bool emitBait = Internal::ShouldEmitBait(
        needsBaitCharPrefix_, bsCount, text,
        suggestKeepChars_.load(std::memory_order_acquire),
        suppressBait_.load(std::memory_order_acquire));
    if (emitBait) {
        if (bi + 2 > kMaxBatch) return false;
        bsBuf[bi++] = MakeUnicodeChar(0x202F, /*keyup=*/false);
        bsBuf[bi++] = MakeUnicodeChar(0x202F, /*keyup=*/true);
        ++bsCount;  // extra BS to delete the bait char
    }
    for (std::size_t k = 0; k < bsCount; ++k) {
        if (bi + 2 > kMaxBatch) return false;
        bsBuf[bi++] = MakeKey(VK_BACK, /*keyup=*/false);
        bsBuf[bi++] = MakeKey(VK_BACK, /*keyup=*/true);
    }
    if (bi > 0) {
        if (!Internal::TrackedSendInput(bsBuf.data(), static_cast<UINT>(bi))) {
            return false;
        }
    }

    // Batch 2: chars (only if text non-empty). Sleep only when both
    // batches present — pure-BS or pure-text needs no inter-batch gap.
    if (!text.empty()) {
        if (bi > 0) {
            Internal::g_sleep(static_cast<DWORD>(sleepMs_));
        }
        std::array<INPUT, kMaxBatch> charBuf{};
        std::size_t ci = 0;
        for (WCHAR ch : text) {
            if (ci + 2 > kMaxBatch) return false;
            charBuf[ci++] = MakeUnicodeChar(ch, /*keyup=*/false);
            charBuf[ci++] = MakeUnicodeChar(ch, /*keyup=*/true);
        }
        if (ci == 0) return true;
        if (!Internal::TrackedSendInput(charBuf.data(), static_cast<UINT>(ci))) {
            return false;
        }
    }
    return true;
}

void SplitDispatchInjector::SendKey(unsigned short vkCode) noexcept {
    INPUT events[2] = {
        MakeKey(static_cast<WORD>(vkCode), /*keyup=*/false),
        MakeKey(static_cast<WORD>(vkCode), /*keyup=*/true),
    };
    (void)Internal::TrackedSendInput(events, 2);
}

}  // namespace NextKey::Output
