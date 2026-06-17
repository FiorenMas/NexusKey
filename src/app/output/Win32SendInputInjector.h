// src/app/output/Win32SendInputInjector.h
//
// Default catch-all injector. Batch SendInput. Covers Win32 plain Edit,
// Chrome omnibox, Notepad++, Chromium renderer textareas (Gmail / ChatGPT)
// — ~85% of host classes.
//
// Optional bait-char prefix for Chromium autocomplete-dismiss quirk
// (replicates HookEngine::SendBackspaces line ~3121 logic).
//
// Spec: docs/plans/sprint-2-output-injector.md §2.2
#pragma once

#include "IOutputInjector.h"

namespace NextKey::Output {

class Win32SendInputInjector final : public IOutputInjector {
public:
    explicit Win32SendInputInjector(bool needsBaitCharPrefix) noexcept
        : needsBaitCharPrefix_(needsBaitCharPrefix) {}

    bool Replace(std::size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;

    // Batch SendInput drains within ~20-25 ms on real hosts under load.
    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{30};
    }

    // Channel trait — exposes the constructor-supplied bait flag.
    // HookEngine uses this to skip game-compat reinjectVk for Chromium
    // browsers (the bait char already handles suggest dismiss).
    bool NeedsBaitCharPrefix() const noexcept override {
        return needsBaitCharPrefix_;
    }
    // HasMultiProcessRenderer() inherits the base default (false) —
    // Win32 batched dispatch is for vanilla single-process renderers.

private:
    bool needsBaitCharPrefix_;
};

}  // namespace NextKey::Output
