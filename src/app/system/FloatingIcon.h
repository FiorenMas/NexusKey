// VKey - Floating V/E Icon Overlay
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <Windows.h>
#include <climits>
#include <cstdint>

namespace NextKey {

/// Lightweight floating overlay showing V/E input mode.
/// Pure GDI — no Sciter, no GDI+.
/// Draggable via HTCAPTION, per-pixel alpha via UpdateLayeredWindow.
class FloatingIcon {
public:
    FloatingIcon() = default;
    ~FloatingIcon();

    FloatingIcon(const FloatingIcon&) = delete;
    FloatingIcon& operator=(const FloatingIcon&) = delete;

    /// Create the overlay window (hidden by default)
    [[nodiscard]] bool Create(HINSTANCE hInstance, bool initialVietnamese = true);

    /// Destroy the overlay window and free resources
    void Destroy() noexcept;

    /// Update displayed mode (no-op if unchanged)
    void SetVietnameseMode(bool enabled) noexcept;

    /// Show or hide the overlay
    void SetVisible(bool visible) noexcept;

    /// Restore saved position (INT32_MIN = use default bottom-right)
    void SetPosition(int x, int y) noexcept;

    [[nodiscard]] bool IsCreated() const noexcept { return hwnd_ != nullptr; }
    [[nodiscard]] bool IsVisible() const noexcept { return visible_; }
    [[nodiscard]] int GetPosX() const noexcept { return posX_; }
    [[nodiscard]] int GetPosY() const noexcept { return posY_; }

    static constexpr int ICON_SIZE = 24;

private:
    void RenderCachedBitmaps() noexcept;
    void ApplyBitmap() noexcept;
    void ComputeDefaultPosition() noexcept;
    void ClampToWorkArea() noexcept;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    bool vietnameseMode_ = true;
    bool visible_ = false;
    int posX_ = INT32_MIN;
    int posY_ = INT32_MIN;

    HBITMAP bmpV_ = nullptr;
    HBITMAP bmpE_ = nullptr;

    static constexpr int MARGIN = 20;
};

}  // namespace NextKey
