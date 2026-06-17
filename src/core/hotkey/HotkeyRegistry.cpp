// VKey — Unified hotkey registry implementation.
// SPDX-License-Identifier: AGPL-3.0-only

#include "core/hotkey/HotkeyRegistry.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <toml.hpp>

namespace NextKey {

namespace {

// Win32 VK constants reproduced here so this file stays Linux-portable.
constexpr uint32_t kVkShift   = 0x10;
constexpr uint32_t kVkControl = 0x11;
constexpr uint32_t kVkMenu    = 0x12;  // Alt
constexpr uint32_t kVkLwin    = 0x5B;
constexpr uint32_t kVkRwin    = 0x5C;
constexpr uint32_t kVkEscape  = 0x1B;

[[nodiscard]] std::string_view IntentToString(Intent intent) noexcept {
    switch (intent) {
    case Intent::CancelComposition: return "cancel-composition";
    case Intent::SkipMacro:         return "skip-macro";
    case Intent::ToggleEnabled:     return "toggle-enabled";
    }
    return "";
}

[[nodiscard]] bool ParseIntent(std::string_view s, Intent& out) noexcept {
    if (s == "cancel-composition") { out = Intent::CancelComposition; return true; }
    if (s == "skip-macro")         { out = Intent::SkipMacro;         return true; }
    if (s == "toggle-enabled")     { out = Intent::ToggleEnabled;     return true; }
    return false;
}

// Snake-case TOML key for the [hotkey_state] section. Separate from the
// kebab-case wire format (`cancel-composition`) used in [[hotkeys]].intent
// values — TOML keys conventionally use underscores here.
[[nodiscard]] const char* IntentStateKey(Intent intent) noexcept {
    switch (intent) {
    case Intent::CancelComposition: return "cancel_composition";
    case Intent::SkipMacro:         return "skip_macro";
    case Intent::ToggleEnabled:     return "toggle_enabled";
    }
    return "";
}

}  // namespace

bool IsModifierKey(uint32_t vk) noexcept {
    return vk == kVkShift || vk == kVkControl || vk == kVkMenu
        || vk == kVkLwin  || vk == kVkRwin;
}

bool HotkeyRegistry::Matches(Intent   intent,
                           uint32_t vk,
                           uint32_t mods,
                           bool     isDoubleTap,
                           bool     keyUp) const noexcept {
    // Disabled intent never fires — bindings are preserved on disk so the
    // user can flip the toggle without losing custom triggers.
    if (!IsEnabled(intent)) return false;

    // Contract: caller frames each event. Tap/Chord callers ask on DOWN
    // (keyUp=false, isDoubleTap=false). Modifier-alone callers ask only after
    // verifying a "clean" modifier up (keyUp=true, isDoubleTap=false).
    // Double-tap callers ask after detecting the 2nd tap within the timing
    // window (isDoubleTap=true; existing NexusKey UX fires on the 2nd release,
    // so keyUp=true in practice — but Matches() doesn't require it).
    const auto it = triggers_.find(intent);
    if (it == triggers_.end()) return false;

    for (const Trigger& t : it->second) {
        if (t.vk != vk) continue;

        if (t.doubleTap) {
            // Double-tap fires only on caller's explicit isDoubleTap signal.
            if (isDoubleTap && mods == t.mods) return true;
            continue;
        }

        if (IsModifierKey(t.vk)) {
            // Modifier-vk triggers fire on UP with exact mods match. mods=0 ⇒
            // modifier-alone ("Ctrl alone"); mods≠0 ⇒ modifier-combo
            // ("Ctrl+Shift" stored as {vk=Shift, mods=Ctrl}). Caller passes
            // the OTHER held modifiers — excluding the one being released.
            if (keyUp && !isDoubleTap && mods == t.mods) return true;
            continue;
        }

        // Plain tap or chord with a main key: fire on DOWN with exact mods match.
        if (!keyUp && !isDoubleTap && mods == t.mods) return true;
    }
    return false;
}

HotkeyRegistry HotkeyRegistry::Defaults() {
    HotkeyRegistry cfg;
    cfg.AddTrigger(Intent::CancelComposition, Trigger{kVkEscape, 0, false});
    cfg.AddTrigger(Intent::SkipMacro,         Trigger{kVkEscape, 0, false});
    cfg.AddTrigger(Intent::ToggleEnabled,     Trigger{kVkControl, 0, false});  // implicit modifier-alone
    cfg.AddTrigger(Intent::ToggleEnabled,     Trigger{kVkMenu,    0, true});   // 2×Alt
    // Explicit enabled bits so a Save-round-trip emits them in [hotkey_state]
    // rather than relying on IsEnabled()'s default-true fallback.
    cfg.SetEnabled(Intent::CancelComposition, true);
    cfg.SetEnabled(Intent::SkipMacro,         true);
    cfg.SetEnabled(Intent::ToggleEnabled,     true);
    return cfg;
}

HotkeyRegistry HotkeyRegistry::FromLegacyFields(
    bool    escRestoreRawEnabled,
    bool    tempOffMacroByEsc,
    uint8_t tempOffMethodValue) noexcept {
    HotkeyRegistry cfg;
    // Bindings only when the legacy toggle was ON — honors user's prior
    // "disabled = no binding" choice. The enabled flag mirrors the same value
    // so v2→v3 round-trips are semantically identical.
    if (escRestoreRawEnabled) {
        cfg.AddTrigger(Intent::CancelComposition, Trigger{kVkEscape, 0, false});
    }
    if (tempOffMacroByEsc) {
        cfg.AddTrigger(Intent::SkipMacro, Trigger{kVkEscape, 0, false});
    }
    // tempOffMethod: 0=None, 1=DupAlt, 2=Ctrl (must match TempOffMethod enum
    // in core/config/TypingConfig.h)
    switch (tempOffMethodValue) {
    case 1:  // DupAlt
        cfg.AddTrigger(Intent::ToggleEnabled, Trigger{kVkMenu, 0, /*doubleTap=*/true});
        break;
    case 2:  // Ctrl
        cfg.AddTrigger(Intent::ToggleEnabled, Trigger{kVkControl, 0, /*doubleTap=*/false});
        break;
    case 0:  // None — no toggle binding
    default:
        break;
    }
    cfg.SetEnabled(Intent::CancelComposition, escRestoreRawEnabled);
    cfg.SetEnabled(Intent::SkipMacro,         tempOffMacroByEsc);
    cfg.SetEnabled(Intent::ToggleEnabled,     tempOffMethodValue != 0);
    return cfg;
}

const std::vector<Trigger>& HotkeyRegistry::TriggersFor(Intent intent) const noexcept {
    static const std::vector<Trigger> kEmpty;
    const auto it = triggers_.find(intent);
    return it == triggers_.end() ? kEmpty : it->second;
}

void HotkeyRegistry::AddTrigger(Intent intent, Trigger trigger) {
    triggers_[intent].push_back(trigger);
}

bool HotkeyRegistry::IsEnabled(Intent intent) const noexcept {
    const auto it = enabled_.find(intent);
    return it == enabled_.end() ? true : it->second;  // missing → enabled by default
}

void HotkeyRegistry::SetEnabled(Intent intent, bool enabled) noexcept {
    enabled_[intent] = enabled;
}

void HotkeyRegistry::Clear() noexcept {
    triggers_.clear();
    enabled_.clear();  // back to all-enabled defaults
}

void HotkeyRegistry::Load(const toml::array& cfg) {
    triggers_.clear();
    for (const auto& node : cfg) {
        const toml::table* row = node.as_table();
        if (!row) continue;

        const auto intentNode = row->get("intent");
        const auto triggerNode = row->get("trigger");
        if (!intentNode || !triggerNode) continue;

        const auto* intentStr = intentNode->as_string();
        const auto* triggerTbl = triggerNode->as_table();
        if (!intentStr || !triggerTbl) continue;

        Intent intent;
        if (!ParseIntent(intentStr->get(), intent)) continue;  // unknown intent — skip

        Trigger t;
        if (const auto* vk = triggerTbl->get_as<int64_t>("vk")) {
            t.vk = static_cast<uint32_t>(vk->get());
        } else {
            continue;  // vk required
        }
        if (t.vk == 0) continue;  // reject vk=0

        if (const auto* mods = triggerTbl->get_as<int64_t>("mods")) {
            t.mods = static_cast<uint32_t>(mods->get());
        }
        if (const auto* dt = triggerTbl->get_as<bool>("double_tap")) {
            t.doubleTap = dt->get();
        }

        AddTrigger(intent, t);
    }
}

void HotkeyRegistry::Save(toml::array& cfg) const {
    for (Intent intent : kAllIntents) {
        const auto it = triggers_.find(intent);
        if (it == triggers_.end()) continue;
        for (const Trigger& t : it->second) {
            toml::table row;
            row.insert("intent", std::string(IntentToString(intent)));
            toml::table trig;
            trig.insert("vk",   static_cast<int64_t>(t.vk));
            trig.insert("mods", static_cast<int64_t>(t.mods));
            if (t.doubleTap) {
                trig.insert("double_tap", true);
            }
            row.insert("trigger", std::move(trig));
            cfg.push_back(std::move(row));
        }
    }
}

void HotkeyRegistry::LoadEnabled(const toml::table& tbl) {
    enabled_.clear();
    for (Intent intent : kAllIntents) {
        if (const auto* node = tbl.get_as<bool>(IntentStateKey(intent))) {
            enabled_[intent] = node->get();
        }
        // Missing key → IsEnabled() defaults to true.
    }
}

void HotkeyRegistry::SaveEnabled(toml::table& tbl) const {
    for (Intent intent : kAllIntents) {
        tbl.insert_or_assign(IntentStateKey(intent), IsEnabled(intent));
    }
}

}  // namespace NextKey
