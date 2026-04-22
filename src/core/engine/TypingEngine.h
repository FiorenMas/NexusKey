// NexusKey - Typing Engine Header (unified Telex/VNI/Combined)
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
// Dual-licensed: GPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.

#pragma once

#include "IInputEngine.h"
#include "EngineHelpers.h"
#include "SpellChecker.h"
#include "EnglishProtection.h"
#include "core/config/TypingConfig.h"
#include <vector>
#include <string>

namespace NextKey {

// =============================================================================
// Unified types (shared by all input methods)
// =============================================================================

/// Modifier type for Vietnamese vowels
enum class Modifier : uint8_t {
    None,       // a e i o u y
    Circumflex, // â ê ô
    Breve,      // ă
    Horn,       // ơ ư
    Stroke      // đ (dd / d9)
};

/// Tone type for Vietnamese
enum class Tone : uint8_t {
    None,   // no tone
    Acute,  // sắc (Telex: s, VNI: 1)
    Grave,  // huyền (Telex: f, VNI: 2)
    Hook,   // hỏi (Telex: r, VNI: 3)
    Tilde,  // ngã (Telex: x, VNI: 4)
    Dot     // nặng (Telex: j, VNI: 5)
};

/// Internal state for each character - THE CORE ABSTRACTION
/// IME converts keys into STATE; letters are merely a consequence.
struct CharState {
    wchar_t base = 0;               // Base letter (lowercase): a e i o u y d or consonant
    Modifier mod = Modifier::None;  // Vowel modifier
    Tone tone = Tone::None;         // Tone mark
    bool isUpper = false;           // Preserve original case
    bool synthetic = false;         // True if created by P8 standalone 'w' (not a real keystroke)
    size_t rawIdx = 0;              // rawInput_ index when this state was created (for backspace sync)
    size_t toneRawIdx = SIZE_MAX;   // rawInput_ index of consumed tone key (for escape removal)

    [[nodiscard]] constexpr bool IsVowel() const noexcept {
        return base == L'a' || base == L'e' || base == L'i' ||
               base == L'o' || base == L'u' || base == L'y';
    }
    [[nodiscard]] constexpr bool CanHaveMod() const noexcept {
        // a -> â, ă; e -> ê; o -> ô, ơ; u -> ư
        return base == L'a' || base == L'e' || base == L'o' || base == L'u';
    }
    [[nodiscard]] constexpr bool IsD() const noexcept { return base == L'd'; }
    [[nodiscard]] constexpr bool IsHorn() const noexcept { return mod == Modifier::Horn; }
    [[nodiscard]] constexpr bool HasModifier() const noexcept { return mod != Modifier::None; }
    [[nodiscard]] constexpr bool HasTone() const noexcept { return tone != Tone::None; }
    [[nodiscard]] constexpr bool IsEmpty() const noexcept { return base == 0; }
};

// =============================================================================
// TypingEngine — unified input method engine (Telex, VNI, Combined)
// =============================================================================

/// Unified input method engine - STATE-BASED ARCHITECTURE
///
/// Key principle: IME tracks STATE, not precomposed Unicode.
/// Unicode is only produced when Peek() or Commit() is called.
///
/// This makes:
/// - Backspace clean (pop entire char state)
/// - Escape clean (retype tone/mod to clear)
/// - No reverse maps needed
/// - TSF composition state always in sync
class TypingEngine : public IInputEngine {
public:
    TypingEngine() : TypingEngine(TypingConfig{}) {}
    explicit TypingEngine(const TypingConfig& config);
    ~TypingEngine() override = default;

    TypingEngine(const TypingEngine&) = delete;
    TypingEngine& operator=(const TypingEngine&) = delete;

    // IInputEngine implementation
    void PushChar(wchar_t c) override;
    void Backspace() override;
    [[nodiscard]] const std::wstring& Peek() const override;
    [[nodiscard]] std::wstring Commit() override;
    void Reset() override;
    [[nodiscard]] size_t Count() const override { return states_.size(); }
    [[nodiscard]] bool HasActiveQuickConsonant() const override { return qc_.hasActive(); }
    [[nodiscard]] bool SeedFromText(const std::wstring& text) override;
    [[nodiscard]] bool IsEnglishWord() const override {
        return engProt_.bias == LanguageBias::HardEnglish;
    }

private:
    // Mode helpers
    [[nodiscard]] bool IsTelexMode() const noexcept {
        return config_.inputMethod != InputMethod::VNI;
    }
    [[nodiscard]] bool IsVniMode() const noexcept {
        return config_.inputMethod == InputMethod::VNI ||
               config_.inputMethod == InputMethod::Combined;
    }

    // Input processing — Telex
    bool ProcessTone(Tone tone, wchar_t keyChar, size_t cachedTarget = SIZE_MAX);
    bool ProcessClearTone();          // z/0 — remove existing tone
    bool ProcessTelexModifier(wchar_t c, wchar_t lower);  // w, [], aa, ee, oo, dd
    void ProcessChar(wchar_t c) { ProcessChar(c, towlower(c), iswupper(c)); }
    void ProcessChar(wchar_t c, wchar_t lower, bool isUpper);

    // Input processing — VNI
    bool ProcessVniModifier(wchar_t c);
    bool ProcessVniHornModifier(wchar_t c);
    bool ProcessVniVowelModifier(Modifier targetMod, wchar_t key);

    // Find target for tone/modifier application
    size_t FindToneTarget() const;
    size_t FindToneTargetClassic() const;
    size_t FindToneTargetModern() const;
    size_t FindToneTargetImpl(const uint8_t table[6][6], bool checkTriphthongs) const;

    // Auto ươ: convert 'uơ' to 'ươ' when followed by another character
    void ApplyAutoUO();

    // Move tone to horn vowel when horn modifier is added
    // Example: "cuả" + w → "cửa" (tone moves from a to ư)
    void RelocateToneToHornVowel();

    // Move tone to correct target after modifier changes priority
    // Example: "chuanr" + a → tone moves from u to â (circumflex has higher priority)
    void RelocateToneToTarget();

    // W-Modifier processing (explicit priority order)
    bool ProcessWModifier(wchar_t c);

    // D-Modifier processing (dd/d9 → đ)
    bool ProcessDModifier(wchar_t c);

    // QU Cluster detection (qu is consonant cluster, u is not vowel)
    bool IsInQUCluster() const;

    // Compose single CharState to Unicode
    static wchar_t Compose(const CharState& s);

    // Compose all states to string
    const std::wstring& ComposeAll() const;

    // Remove consumed raw entry and adjust all indices
    void EraseConsumedRaw(size_t idx);

    // Spell exclusion: would the modifier key produce an excluded word?
    bool WouldModifierKeyMatchExclusion(wchar_t lower) const;

    // Spell check: validate syllable structure after each keystroke
    void UpdateSpellState();

    // Would applying `newMod` to states_[targetIdx] produce a syllable that is
    // not Invalid per SpellCheck? Returns true when spell-check is disabled
    // (no validation performed). If `clearCircumflexIdx` is a valid index and
    // that state has a Circumflex, it is temporarily cleared for the check
    // (models W-modifier P5 which strips the sister â when applying horn to u).
    // Restores state before returning.
    // SpellCheck::Validate enforces the tone/mod same-vowel invariant, so
    // this also catches typos like "của" + circumflex on 'a' → c,ủ,â.
    [[nodiscard]] bool WouldBeValidSyllable(size_t targetIdx, Modifier newMod,
                                            size_t clearCircumflexIdx = SIZE_MAX);

    // Common guard used by modifier sites (Telex adjacent/cross-vowel,
    // VNI Pass 1): reject when applying `newMod` at `targetIdx` produces an
    // invalid syllable AND the result doesn't match a user spell exclusion.
    // Returns false when spell check is disabled (no validation performed).
    [[nodiscard]] bool ShouldRejectModifier(size_t targetIdx, Modifier newMod,
                                            wchar_t key);

    // State
    std::vector<CharState> states_;   // Internal state buffer
    std::vector<wchar_t> rawInput_;   // Raw keys for escape
    TypingConfig config_;
    bool spellCheckDisabled_ = false; // true when buffer is invalid syllable
    QuickConsonantState qc_;              // Quick consonant expansion state
    EscapeState escape_;                  // Replaces toneEscaped_ + dModifierEscaped_
    wchar_t quickStartKey_ = 0;          // original key for quick start consonant (f/j/w), 0 if none
    EnglishProtectionState engProt_;     // 3-tier English protection state
    mutable std::wstring composeBuf_;    // Reusable buffer for ComposeAll() — avoids heap alloc per Peek()
};

}  // namespace NextKey
