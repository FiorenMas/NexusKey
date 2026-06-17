// VKey — Hotkey label formatter implementation.
// SPDX-License-Identifier: AGPL-3.0-only

#include "core/hotkey/HotkeyLabel.h"

#include <cwctype>

#include "core/config/TypingConfig.h"  // HotkeyConfig::ToMods bit values
#include "core/hotkey/HotkeyRegistry.h"

namespace NextKey {

// Drift guard — TypingConfig.h hardcodes 0x01/0x02/0x04/0x08 for the
// modifier bitmask to avoid pulling HotkeyRegistry.h into every config
// consumer. This assert wires the two headers together so any divergence
// breaks the build instead of silently mismapping flags at runtime.
static_assert(kModCtrl  == 0x01u, "HotkeyConfig::ToMods Ctrl bit drift");
static_assert(kModShift == 0x02u, "HotkeyConfig::ToMods Shift bit drift");
static_assert(kModAlt   == 0x04u, "HotkeyConfig::ToMods Alt bit drift");
static_assert(kModWin   == 0x08u, "HotkeyConfig::ToMods Win bit drift");

namespace {

// Irregular VK → name table. Letters/digits/F-keys/numpad handled by the
// algorithmic branches in VkToKeyName, not listed here.
//
// Two style variants live next to each other so a new VK entry can't be
// added to one and forgotten in the other. Only 5 entries diverge — arrows
// (0x25-0x28) and Delete (0x2E) — everything else mirrors verbatim.
const std::vector<std::pair<uint32_t, const wchar_t*>>& WordsTable() {
    static const std::vector<std::pair<uint32_t, const wchar_t*>> kPairs = {
        {0x08, L"Backspace"}, {0x09, L"Tab"},   {0x0D, L"Enter"},
        {0x10, L"Shift"},     {0x11, L"Ctrl"},  {0x12, L"Alt"},
        {0x13, L"Pause"},     {0x14, L"Caps"},  {0x1B, L"Esc"},
        {0x20, L"Space"},
        {0x21, L"PgUp"},      {0x22, L"PgDn"},  {0x23, L"End"},    {0x24, L"Home"},
        {0x25, L"Left"},      {0x26, L"Up"},    {0x27, L"Right"},  {0x28, L"Down"},
        {0x2C, L"PrtSc"},     {0x2D, L"Insert"}, {0x2E, L"Delete"},
        {0x5B, L"Win"},       {0x5C, L"Win"},   {0x5D, L"Menu"},
        {0xBA, L";"}, {0xBB, L"="}, {0xBC, L","}, {0xBD, L"-"}, {0xBE, L"."}, {0xBF, L"/"},
        {0xC0, L"`"}, {0xDB, L"["}, {0xDC, L"\\"}, {0xDD, L"]"}, {0xDE, L"'"},
    };
    return kPairs;
}

const std::vector<std::pair<uint32_t, const wchar_t*>>& ArrowsTable() {
    static const std::vector<std::pair<uint32_t, const wchar_t*>> kPairs = {
        {0x08, L"Backspace"}, {0x09, L"Tab"},   {0x0D, L"Enter"},
        {0x10, L"Shift"},     {0x11, L"Ctrl"},  {0x12, L"Alt"},
        {0x13, L"Pause"},     {0x14, L"Caps"},  {0x1B, L"Esc"},
        {0x20, L"Space"},
        {0x21, L"PgUp"},      {0x22, L"PgDn"},  {0x23, L"End"},    {0x24, L"Home"},
        {0x25, L"←"},         {0x26, L"↑"},     {0x27, L"→"},      {0x28, L"↓"},
        {0x2C, L"PrtSc"},     {0x2D, L"Insert"}, {0x2E, L"Del"},
        {0x5B, L"Win"},       {0x5C, L"Win"},   {0x5D, L"Menu"},
        {0xBA, L";"}, {0xBB, L"="}, {0xBC, L","}, {0xBD, L"-"}, {0xBE, L"."}, {0xBF, L"/"},
        {0xC0, L"`"}, {0xDB, L"["}, {0xDC, L"\\"}, {0xDD, L"]"}, {0xDE, L"'"},
    };
    return kPairs;
}

[[nodiscard]] std::wstring VkToKeyName(uint32_t vk) {
    if (vk == 0) return L"";

    if (vk >= 0x30 && vk <= 0x39) {                  // '0'..'9'
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= 0x41 && vk <= 0x5A) {                  // 'A'..'Z'
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    if (vk >= 0x60 && vk <= 0x69) {                  // Numpad0..9
        return L"Num" + std::to_wstring(vk - 0x60);
    }
    if (vk >= 0x70 && vk <= 0x87) {                  // F1..F24
        std::wstring s = L"F";
        s += std::to_wstring(vk - 0x6F);
        return s;
    }
    for (const auto& [k, name] : WordsTable()) {
        if (k == vk) return name;
    }

    // Fallback so users see *something* instead of a blank label.
    static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring s = L"VK 0x";
    s += kHex[(vk >> 12) & 0xF];
    s += kHex[(vk >> 8)  & 0xF];
    s += kHex[(vk >> 4)  & 0xF];
    s += kHex[(vk >> 0)  & 0xF];
    return s;
}

void AppendWithPlus(std::wstring& out, std::wstring_view token) {
    if (!out.empty()) out.push_back(L'+');
    out.append(token);
}

}  // namespace

const std::vector<std::pair<uint32_t, const wchar_t*>>& GetVkDisplayNames(
        VkNameStyle style) {
    return (style == VkNameStyle::CompactArrows) ? ArrowsTable() : WordsTable();
}

std::wstring FormatHotkeyLabel(uint32_t vk, uint32_t mods) {
    std::wstring out;
    out.reserve(24);

    if (mods & kModCtrl)  AppendWithPlus(out, L"Ctrl");
    if (mods & kModShift) AppendWithPlus(out, L"Shift");
    if (mods & kModAlt)   AppendWithPlus(out, L"Alt");
    if (mods & kModWin)   AppendWithPlus(out, L"Win");

    if (vk != 0) {
        AppendWithPlus(out, VkToKeyName(vk));
    }
    return out;
}

uint32_t LegacyKeyCharToVk(const std::wstring& s) noexcept {
    if (s.size() != 1) return 0;
    wchar_t c = static_cast<wchar_t>(std::towupper(s[0]));
    // A-Z and 0-9 share code points with VK_A..VK_Z (0x41..0x5A)
    // and VK_0..VK_9 (0x30..0x39). Everything else → 0 (user reassigns).
    if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
        return static_cast<uint32_t>(c);
    }
    return 0;
}

}  // namespace NextKey
