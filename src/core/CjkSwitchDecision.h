// VKey - Pure decision function for CJK auto-switch state machine
// SPDX-License-Identifier: AGPL-3.0-only
//
// Extracted from HookEngine::OnLayoutChanged so the state-machine logic can
// be unit-tested on Linux (HookEngine.cpp itself is Windows-only).
//
// The CJK auto-switch flips vietnameseMode_ to false when the foreground
// thread's keyboard layout is Japanese/Chinese/Korean (LANG_JAPANESE,
// LANG_CHINESE, LANG_KOREAN — see IsIncompatibleLayout in HookEngine.cpp),
// so the hook hot path passes typed keys through to the host CJK IME
// instead of producing Vietnamese output. modeBeforeCjk is saved on enter
// and restored on leave.
//
// Two gates suppress the transition without otherwise changing the cached
// fields:
//   - cjkAutoSwitchEnabled = false: user opted out (UI toggle). Some users
//     keep zh-CN/ja-JP layouts installed but never compose with the host
//     IME, so the auto-suppression is unwanted noise. Skip entirely.
//   - isExcluded = true: the foreground app is already excluded, so
//     NotifyModeChange forces icon E regardless of vietnameseMode_. Layering
//     CJK suppression on top breaks the "leaving excluded" restore — when
//     the user alt-tabs through a shell window (Win+D → Progman) on the way
//     to a normal app, the intermediate compatible layout clears
//     layoutSuppressed prematurely, then re-entering CJK on the next app
//     suppresses vietnameseMode_ that the user has no way to set back.
//     Owner of the icon while excluded is the excluded-app path; CJK stays
//     out of the way.
//   - isForcedVietnamese = true: the foreground app is locked to Vietnamese
//     (per-app hard-V). The user explicitly wants V here, so CJK auto-switch
//     (which would force E on a JA/CN/KO layout) must NOT fight it. Same gate
//     rationale as isExcluded — the forced-V path owns the mode while focused.

#pragma once

#include <cstdint>

namespace NextKey {

enum class CjkTransition : uint8_t {
    None,
    EnterCjk,  // suppress vietnameseMode_
    LeaveCjk,  // restore vietnameseMode_ to modeBeforeCjk
};

enum class CjkBeep : uint8_t {
    Quiet,
    Ok,         // MB_OK — restoring V mode
    Asterisk,   // MB_ICONASTERISK — suppressing to E mode
};

struct CjkSwitchInputs {
    bool isCompatibleNow;       // true if current layout is NOT JA/CN/KO
    bool layoutSuppressed;      // current cached layoutSuppressed_
    bool modeBeforeCjk;         // current cached modeBeforeCjk_
    bool vietnameseMode;        // current vietnameseMode_
    bool isExcluded;            // current isExcludedApp_
    bool isForcedVietnamese;    // current isForcedVnApp_ (per-app hard-V lock)
    bool cjkAutoSwitchEnabled;  // user toggle (TypingConfig::cjkAutoSwitch)
};

struct CjkSwitchOutputs {
    CjkTransition transition = CjkTransition::None;
    bool newLayoutSuppressed = false;  // value to write back to layoutSuppressed_
    bool newModeBeforeCjk = true;      // value to write back to modeBeforeCjk_
    bool newVietnameseMode = true;     // value to write back to vietnameseMode_
    bool needCommitComposition = false;  // caller checks engine_->Count() before committing
    bool needNotifyMode = false;
    CjkBeep beep = CjkBeep::Quiet;
};

/// Pure: no I/O, no syscalls. All side effects are described in the output
/// struct for the caller to apply. Safe to call from any thread.
[[nodiscard]] inline CjkSwitchOutputs DecideCjkSwitch(const CjkSwitchInputs& in) noexcept {
    CjkSwitchOutputs out;
    out.newLayoutSuppressed = in.layoutSuppressed;
    out.newModeBeforeCjk    = in.modeBeforeCjk;
    out.newVietnameseMode   = in.vietnameseMode;

    if (!in.cjkAutoSwitchEnabled) return out;
    if (in.isExcluded || in.isForcedVietnamese) return out;

    if (!in.isCompatibleNow && !in.layoutSuppressed) {
        // ── Entering CJK ──
        out.transition = CjkTransition::EnterCjk;
        out.newLayoutSuppressed = true;
        out.newModeBeforeCjk = in.vietnameseMode;
        out.needCommitComposition = true;
        if (in.vietnameseMode) {
            out.newVietnameseMode = false;
            out.needNotifyMode = true;
            out.beep = CjkBeep::Asterisk;
        }
    } else if (in.isCompatibleNow && in.layoutSuppressed) {
        // ── Leaving CJK ──
        out.transition = CjkTransition::LeaveCjk;
        out.newLayoutSuppressed = false;
        if (in.modeBeforeCjk != in.vietnameseMode) {
            out.newVietnameseMode = in.modeBeforeCjk;
            out.beep = in.modeBeforeCjk ? CjkBeep::Ok : CjkBeep::Asterisk;
        }
        out.needNotifyMode = true;
    }
    return out;
}

}  // namespace NextKey
