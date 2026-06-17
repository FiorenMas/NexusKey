// src/app/output/ClipboardInjector.h
#pragma once

#include "IOutputInjector.h"
#include <windows.h>
#include <chrono>

namespace NextKey::Output {

class ClipboardInjector final : public IOutputInjector {
public:
    ClipboardInjector() noexcept = default;
    ~ClipboardInjector() override = default;

    bool Replace(std::size_t bsCount, std::wstring_view text) noexcept override;
    void SendKey(unsigned short vkCode) noexcept override;

    std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{150};
    }
};

} // namespace NextKey::Output
