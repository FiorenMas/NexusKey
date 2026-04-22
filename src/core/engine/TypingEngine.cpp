// NexusKey - Typing Engine Implementation (unified Telex/VNI/Combined)
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
// Dual-licensed: GPL-3.0 for open-source use, commercial license for proprietary use.
// See LICENSE and LICENSE-COMMERCIAL in the project root.
//
// V3 changes: flat constexpr arrays for O(1) Compose(), stack-allocated
// FindToneTarget(), bounded ApplyAutoUO(), pre-reserved buffers.

#include "TypingEngine.h"
#include "EngineHelpers.h"
#include "VietnameseTables.h"
#include <algorithm>

namespace NextKey {

namespace {

//=============================================================================
// Key mapping helpers
//=============================================================================

// IsVowelChar is shared — defined in VietnameseTables.h

// --- Telex key mapping ---
constexpr Tone TelexKeyToTone(wchar_t c) noexcept {
    switch (c) {
        case L's': case L'S': return Tone::Acute;
        case L'f': case L'F': return Tone::Grave;
        case L'r': case L'R': return Tone::Hook;
        case L'x': case L'X': return Tone::Tilde;
        case L'j': case L'J': return Tone::Dot;
        default: return Tone::None;
    }
}

// --- VNI key mapping ---
constexpr Tone VniKeyToTone(wchar_t c) noexcept {
    switch (c) {
        case L'1': return Tone::Acute;
        case L'2': return Tone::Grave;
        case L'3': return Tone::Hook;
        case L'4': return Tone::Tilde;
        case L'5': return Tone::Dot;
        default:   return Tone::None;
    }
}

constexpr bool IsVniModifierKey(wchar_t c) noexcept {
    return c >= L'6' && c <= L'9';
}

constexpr int ModifierIndex(Modifier mod) noexcept {
    switch (mod) {
        case Modifier::Circumflex: return 0;
        case Modifier::Breve:      return 1;
        case Modifier::Horn:       return 2;
        default:                   return -1;
    }
}

constexpr int ToneIndex(Tone tone) noexcept {
    switch (tone) {
        case Tone::Acute: return 0;
        case Tone::Grave: return 1;
        case Tone::Hook:  return 2;
        case Tone::Tilde: return 3;
        case Tone::Dot:   return 4;
        default:          return -1;
    }
}

}  // namespace

//=============================================================================
// TypingEngine Implementation
//=============================================================================

TypingEngine::TypingEngine(const TypingConfig& config) : config_(config) {
    states_.reserve(8);
    rawInput_.reserve(12);
    Reset();
}

//-----------------------------------------------------------------------------
// Main Entry Point
//-----------------------------------------------------------------------------

void TypingEngine::PushChar(wchar_t c) {
    // No buffer cap: game-compatible Telex keeps V mode active while WASD-spamming
    // can exceed any fixed limit. Relies on Reset() hooks (focus, click, space, enter)
    // to bound growth in practice.

    rawInput_.push_back(c);
    qc_.onlyQC = false;  // Any new char clears the flag

    // 0a. Quick start consonant: f→ph, j→gi, w→qu (only at word start)
    if (config_.quickStartConsonant && states_.empty()) {
        wchar_t lower = towlower(c);
        wchar_t first = 0, second = 0;
        if (lower == L'f') { first = L'p'; second = L'h'; }
        else if (lower == L'j') { first = L'g'; second = L'i'; }
        else if (lower == L'w') { first = L'q'; second = L'u'; }
        if (first) {
            bool upper = iswupper(c);
            ProcessChar(upper ? towupper(first) : first);
            ProcessChar(second);
            quickStartKey_ = c;  // Remember original key for undo
            UpdateSpellState();
            return;
        }
    }

    // 0a-cont. Undo quick start consonant if next char is not a vowel
    // e.g., f→ph, then 't' → undo to "ft" (not "pht")
    if (quickStartKey_ != 0) {
        wchar_t savedKey = quickStartKey_;
        quickStartKey_ = 0;  // Clear before any further processing
        if (!IsVowelChar(c)) {
            states_.clear();
            rawInput_.clear();
            rawInput_.push_back(savedKey);
            ProcessChar(savedKey);
            rawInput_.push_back(c);
            // Fall through to normal processing below
        }
    }

    // 0b. Quick consonant: cc→ch, gg→gi, nn→ng, kk→kh, qq→qu, pp→ph, tt→th
    // Skip if backspace just undid a quick consonant (let user type the literal)
    bool quickEscaped = qc_.escaped;
    qc_.escaped = false;

    // Suppress consecutive re-triggering: after cc→ch, skip quick consonant
    // while the user keeps pressing the same key (e.g., cccc → chcc, not chch)
    wchar_t lower = towlower(c);
    bool isUpper = iswupper(c);
    if (qc_.lastKey != 0) {
        if (lower == qc_.lastKey) {
            quickEscaped = true;  // Reuse escape flag to skip quick consonant
        } else {
            qc_.lastKey = 0;  // Different key, allow future expansions
        }
    }

    if (config_.quickConsonant && !states_.empty() && !quickEscaped) {
        const CharState& last = states_.back();
        if (!last.IsVowel() && !last.IsD()) {
            wchar_t replacement = 0;
            if (last.base == L'c' && lower == L'c') replacement = L'h';
            else if (last.base == L'g' && lower == L'g') replacement = L'i';
            else if (last.base == L'n' && lower == L'n') replacement = L'g';
            else if (last.base == L'k' && lower == L'k') replacement = L'h';
            else if (last.base == L'q' && lower == L'q') replacement = L'u';
            else if (last.base == L'p' && lower == L'p') replacement = L'h';
            else if (last.base == L't' && lower == L't') replacement = L'h';
            if (replacement) {
                if (states_.size() == 1) qc_.onlyQC = true;
                qc_.resultIndex = states_.size();  // Index of the char about to be added
                qc_.lastKey = lower;  // Suppress re-trigger — save ORIGINAL key before update
                c = isUpper ? towupper(replacement) : replacement;
                lower = towlower(c);  // Update for downstream ProcessChar
                isUpper = iswupper(c);
            }
        }
        // uu→ươ: apply horn to existing 'u', then insert 'ơ'
        // Guard: don't expand if last 3 vowels form a triphthong (e.g., khuyu + u)
        else if (last.IsVowel() && last.base == L'u' && last.mod == Modifier::None && lower == L'u') {
            size_t n = states_.size();
            bool triphthong = n >= 3 && states_[n - 3].IsVowel() && states_[n - 2].IsVowel() &&
                              IsTriphthong(states_[n - 3].base, states_[n - 2].base, last.base);
            if (!triphthong) {
                bool upper = iswupper(c);
                states_.back().mod = Modifier::Horn;  // u→ư
                CharState s;
                s.base = L'o';
                s.mod = Modifier::Horn;  // ơ
                s.isUpper = upper;
                s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
                states_.push_back(s);
                qc_.resultIndex = states_.size() - 1;  // Index of the ơ just added
                if (states_.size() == 2) qc_.onlyQC = true;
                qc_.lastKey = lower;  // Suppress re-trigger on consecutive same key
                UpdateSpellState();
                return;
            }
            // Triphthong — fall through to normal processing
        }
    }

    // 1a. Clear tone: Telex 'z' / VNI '0'
    if (!states_.empty()) {
        bool isClearToneKey = (IsTelexMode() && lower == L'z') ||
                              (IsVniMode() && c == L'0');
        if (isClearToneKey) {
            if (config_.spellCheckEnabled && spellCheckDisabled_) {
                ProcessChar(c);
                UpdateSpellState();
                return;
            }
            if (ProcessClearTone()) {
                UpdateSpellState();
                return;
            }
        }
    }

    // 1b. Tone keys — determine tone from Telex or VNI key mapping
    Tone requestedTone = Tone::None;
    bool isTelexTone = false;
    if (IsTelexMode() && !states_.empty()) {
        requestedTone = TelexKeyToTone(c);
        isTelexTone = (requestedTone != Tone::None);
    }
    if (requestedTone == Tone::None && IsVniMode() && !states_.empty()) {
        requestedTone = VniKeyToTone(c);
    }

    if (requestedTone != Tone::None) {
        // All "treat as literal" paths share the same two operations.
        auto asLiteral = [&] { ProcessChar(c, lower, isUpper); UpdateSpellState(); };

        // Cache FindToneTarget from spell-check gate to avoid redundant call in ProcessTone.
        size_t cachedToneTarget = SIZE_MAX;
        bool hasCachedTarget = false;

        if (config_.spellCheckEnabled && spellCheckDisabled_) {
            cachedToneTarget = FindToneTarget();
            hasCachedTarget = true;
            size_t t = cachedToneTarget;
            bool isEscape = (t != SIZE_MAX && states_[t].tone == requestedTone);
            bool matchesExclusion = false;
            if (!isEscape && !config_.spellExclusions.empty() && t != SIZE_MAX) {
                CharState tentative = states_[t];
                tentative.tone = requestedTone;
                wchar_t tonedCh = Compose(tentative);
                matchesExclusion = WouldToneMatchExclusion(states_.data(), states_.size(),
                    config_.spellExclusions,
                    [](const CharState& s) { return Compose(s); },
                    t, tonedCh);
            }
            if (!isEscape && !matchesExclusion) { asLiteral(); return; }
        }
        // Tone escape: user pressed same tone twice — blocks Vietnamese.
        if (escape_.isEscaped())                                { asLiteral(); return; }
        // English word block: raw prefix check (Telex keys only — digits don't appear in English).
        if (isTelexTone && config_.spellCheckEnabled &&
            IsBlockedEnglishTone(rawInput_.data(), rawInput_.size())) {
            bool overridden = false;
            if (!config_.spellExclusions.empty()) {
                if (!hasCachedTarget) { cachedToneTarget = FindToneTarget(); hasCachedTarget = true; }
                if (cachedToneTarget != SIZE_MAX) {
                    CharState tentative = states_[cachedToneTarget];
                    tentative.tone = requestedTone;
                    wchar_t tonedCh = Compose(tentative);
                    overridden = WouldToneMatchExclusion(states_.data(), states_.size(),
                        config_.spellExclusions,
                        [](const CharState& s) { return Compose(s); },
                        cachedToneTarget, tonedCh);
                }
            }
            if (!overridden) { asLiteral(); return; }
        }
        // English Protection: always active, independent of spell check.
        if (!config_.allowEnglishBypass) {
            if (engProt_.bias == LanguageBias::HardEnglish)         { asLiteral(); return; }
            if (engProt_.bias == LanguageBias::SoftEnglish) {
                if (!UpdateToneInsistence(c, engProt_))              { asLiteral(); return; }
            }
            // Structural V+C+V check:
            // Telex: uses IsHardEnglishToneContext (gated by IsHardEnglishEnd on the key)
            // VNI: uses HasStructuralVCVPattern (no key guard — digits aren't word-ending chars)
            if (isTelexTone) {
                if (IsHardEnglishToneContext(states_.data(), states_.size(), c)) {
                    engProt_.bias = LanguageBias::HardEnglish;
                    asLiteral(); return;
                }
            } else if (states_.size() >= 4) {
                if (HasStructuralVCVPattern(states_.data(), states_.size())) {
                    engProt_.bias = LanguageBias::HardEnglish;
                    asLiteral(); return;
                }
            }
            if (HasInvalidAdjacentVowelPair(states_.data(), states_.size())) {
                engProt_.bias = LanguageBias::HardEnglish;
                asLiteral(); return;
            }
        }
        // Pre-tone stop-final check (spellCheck path only):
        if (config_.spellCheckEnabled && !spellCheckDisabled_) {
            if (requestedTone == Tone::Grave || requestedTone == Tone::Hook ||
                    requestedTone == Tone::Tilde) {
                if (HasStopFinalCoda(states_.data(), states_.size())) {
                    asLiteral(); return;
                }
            }
        }
        if (ProcessTone(requestedTone, c, hasCachedTarget ? cachedToneTarget : SIZE_MAX)) {
            if (!escape_.isEscaped()) {
                engProt_.bias = LanguageBias::Vietnamese;
            } else {
                RecalcEnglishBias(states_.data(), states_.size(), engProt_);
                if (IsTelexMode()) CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
            }
            ApplyAutoUO();
            UpdateSpellState();
            return;
        }
    }

    // 2a. Telex modifier keys (w, [], aa, ee, oo, dd)
    if (IsTelexMode()) {
        // Pre-check for 'd' modifier: if dd→đ would fire AND there's already a
        // consonant in coda position, adding 'd' forms an invalid coda like "pd".
        if (lower == L'd' && engProt_.bias != LanguageBias::HardEnglish && states_.size() >= 3) {
            size_t dTarget = FindStrokeDTarget(states_.data(), states_.size());
            if (dTarget != SIZE_MAX && IsStrokeDBlockedByCoda(states_.data(), states_.size(), dTarget)) {
                engProt_.bias = LanguageBias::HardEnglish;
            }
        }
        bool blockModifiers = escape_.isEscaped() ||
            (!config_.allowEnglishBypass && engProt_.bias == LanguageBias::HardEnglish);
        if (!blockModifiers && config_.spellCheckEnabled &&
            IsBlockedEnglishModifier(rawInput_.data(), rawInput_.size())) {
            blockModifiers = true;
        }
        if (blockModifiers && !escape_.isEscaped() && !config_.spellExclusions.empty()) {
            if (WouldModifierKeyMatchExclusion(lower))
                blockModifiers = false;
        }
        if (blockModifiers) {
            // Don't try Telex modifiers — treat as literal
        } else if (config_.spellCheckEnabled && spellCheckDisabled_) {
            bool canEscape = false;
            if (lower == L'w') {
                canEscape = HasEscapableModifier(states_.data(), states_.size(), Modifier::Horn) ||
                            HasEscapableModifier(states_.data(), states_.size(), Modifier::Breve);
            } else if (lower == L'd') {
                canEscape = HasEscapableModifier(states_.data(), states_.size(), Modifier::Stroke, true);
            } else if (IsVowelChar(c) && !states_.empty()) {
                const CharState& last = states_.back();
                if (last.IsVowel() && last.base == lower && last.mod == Modifier::Circumflex)
                    canEscape = true;
            }
            if (!canEscape) {
                canEscape = WouldModifierKeyMatchExclusion(lower);
            }
            if (canEscape && ProcessTelexModifier(c, lower)) {
                engProt_.bias = LanguageBias::Vietnamese;
                ApplyAutoUO();
                UpdateSpellState();
                return;
            }
        } else if (ProcessTelexModifier(c, lower)) {
            engProt_.bias = LanguageBias::Vietnamese;
            ApplyAutoUO();
            UpdateSpellState();
            return;
        }
    }

    // 2b. VNI modifier keys (6, 7, 8, 9) — only in VNI/Combined mode
    if (IsVniMode() && IsVniModifierKey(c)) {
        // Pre-check for '9' (stroke): same coda check as Telex 'd'
        if (c == L'9' && engProt_.bias != LanguageBias::HardEnglish && states_.size() >= 3) {
            size_t dTarget = FindStrokeDTarget(states_.data(), states_.size());
            if (dTarget != SIZE_MAX && IsStrokeDBlockedByCoda(states_.data(), states_.size(), dTarget)) {
                engProt_.bias = LanguageBias::HardEnglish;
            }
        }
        bool blockVni = escape_.isEscaped() ||
            (!config_.allowEnglishBypass && engProt_.bias == LanguageBias::HardEnglish);
        // Spell exclusion override for VNI modifiers (same as Telex path)
        if (blockVni && !escape_.isEscaped() && !config_.spellExclusions.empty()) {
            if (WouldModifierKeyMatchExclusion(lower))
                blockVni = false;
        }
        if (blockVni) {
            // Don't try VNI modifiers — treat as literal
        } else if (config_.spellCheckEnabled && spellCheckDisabled_) {
            // Allow VNI modifier escape or spell exclusion match
            bool canEscape = false;
            Modifier escMod = Modifier::None;
            if (c == L'6') escMod = Modifier::Circumflex;
            else if (c == L'7') escMod = Modifier::Horn;
            else if (c == L'8') escMod = Modifier::Breve;
            else if (c == L'9') escMod = Modifier::Stroke;
            if (escMod == Modifier::Stroke) {
                canEscape = HasEscapableModifier(states_.data(), states_.size(), escMod, true);
            } else if (escMod != Modifier::None) {
                canEscape = HasEscapableModifier(states_.data(), states_.size(), escMod);
            }
            if (!canEscape) {
                canEscape = WouldModifierKeyMatchExclusion(lower);
            }
            if (canEscape && ProcessVniModifier(c)) {
                engProt_.bias = LanguageBias::Vietnamese;
                ApplyAutoUO();
                UpdateSpellState();
                return;
            }
        } else if (ProcessVniModifier(c)) {
            engProt_.bias = LanguageBias::Vietnamese;
            ApplyAutoUO();
            UpdateSpellState();
            return;
        }
    }

    // 2c. Quick end consonant: g→ng, h→nh, k→ch (after vowel)
    if (config_.quickEndConsonant && !states_.empty() && states_.back().IsVowel()) {
        wchar_t first = 0, second = 0;
        if (lower == L'g') { first = L'n'; second = L'g'; }
        else if (lower == L'h') { first = L'n'; second = L'h'; }
        else if (lower == L'k') { first = L'c'; second = L'h'; }
        if (first) {
            ProcessChar(first);
            ProcessChar(second);
            UpdateSpellState();
            return;
        }
    }

    // 3. Regular character
    ProcessChar(c, lower, isUpper);
    RelocateToneToTarget();
    ApplyAutoUO();
    UpdateSpellState();

    // English Protection: re-evaluate bias after adding character
    // (always active — independent of spell check setting)
    CheckEnglishBias(states_.data(), states_.size(), engProt_);
    if (IsTelexMode()) CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
}

//-----------------------------------------------------------------------------
// Tone Processing
//-----------------------------------------------------------------------------

bool TypingEngine::ProcessTone(Tone newTone, wchar_t keyChar, size_t cachedTarget) {
    if (newTone == Tone::None) return false;

    size_t targetIdx = (cachedTarget != SIZE_MAX) ? cachedTarget : FindToneTarget();
    if (targetIdx == SIZE_MAX) return false;

    CharState& target = states_[targetIdx];

    // Escape: same tone → clear tone and add key as character
    if (target.tone == newTone) {
        target.tone = Tone::None;
        // Remove consumed first-tone entry from rawInput_ so auto-restore
        // gives "user" instead of "usser" for u-s-s-e-r
        if (target.toneRawIdx != SIZE_MAX) {
            EraseConsumedRaw(target.toneRawIdx);
        }
        target.toneRawIdx = SIZE_MAX;
        ProcessChar(keyChar);
        escape_.escape(EscapeKind::Tone);  // Signal caller: user canceled tone
        return true;
    }

    // Apply or replace tone
    target.tone = newTone;
    target.toneRawIdx = rawInput_.size() - 1;
    escape_.clear();
    return true;
}

//-----------------------------------------------------------------------------
// Clear Tone (z key) — remove any existing tone
//-----------------------------------------------------------------------------

bool TypingEngine::ProcessClearTone() {
    size_t targetIdx = FindToneTarget();
    if (targetIdx == SIZE_MAX) return false;

    CharState& target = states_[targetIdx];
    if (target.tone == Tone::None) return false;

    target.tone = Tone::None;
    target.toneRawIdx = SIZE_MAX;
    return true;
}

//-----------------------------------------------------------------------------
// Modifier Processing (W, [], AA, EE, OO, DD) - TABLE-DRIVEN
//-----------------------------------------------------------------------------

bool TypingEngine::ProcessTelexModifier(wchar_t c, wchar_t lower) {

    // Handle bracket keys: [ → ơ, ] → ư (full Telex only)
    if (config_.inputMethod != InputMethod::SimpleTelex) {
        if (c == L'[') {
            // Escape: [[ → undo inserted ơ, produce literal '['
            if (!states_.empty() && rawInput_.size() >= 2 &&
                rawInput_[rawInput_.size() - 2] == L'[') {
                CharState& last = states_.back();
                if (last.base == L'o' && last.mod == Modifier::Horn) {
                    size_t consumedIdx = last.rawIdx;
                    states_.pop_back();
                    EraseConsumedRaw(consumedIdx);
                    ProcessChar(c);
                    escape_.escape(EscapeKind::Horn);
                    return true;
                }
            }
            // [ → insert 'ơ' (o with horn)
            CharState s;
            s.base = L'o';
            s.mod = Modifier::Horn;
            s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
            states_.push_back(s);
            return true;
        }
        if (c == L']') {
            // Escape: ]] → undo inserted ư, produce literal ']'
            if (!states_.empty() && rawInput_.size() >= 2 &&
                rawInput_[rawInput_.size() - 2] == L']') {
                CharState& last = states_.back();
                if (last.base == L'u' && last.mod == Modifier::Horn) {
                    size_t consumedIdx = last.rawIdx;
                    states_.pop_back();
                    EraseConsumedRaw(consumedIdx);
                    ProcessChar(c);
                    escape_.escape(EscapeKind::Horn);
                    return true;
                }
            }
            // ] → insert 'ư' (u with horn)
            CharState s;
            s.base = L'u';
            s.mod = Modifier::Horn;
            s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
            states_.push_back(s);
            return true;
        }
    }

    // Handle 'w' modifier
    if (lower == L'w') {
        return ProcessWModifier(c);
    }

    // Handle double vowel → circumflex (aa→â, ee→ê, oo→ô)
    if (IsVowelChar(c) && !states_.empty()) {
        CharState& last = states_.back();
        bool isCircumflexBase = (lower == L'a' || lower == L'e' || lower == L'o');

        if (last.IsVowel() && last.base == lower && isCircumflexBase) {
            // Guard: don't apply circumflex if last 3 vowels form a triphthong
            // e.g., "ngoeo" + 'o' → consume but don't modify (triphthong complete)
            size_t n = states_.size();
            if (n >= 3 && states_[n - 3].IsVowel() && states_[n - 2].IsVowel() &&
                IsTriphthong(states_[n - 3].base, states_[n - 2].base, last.base)) {
                return true;  // Consume keystroke, no state change
            }
            // Escape: already has circumflex (adjacent case — last char, no coda possible)
            if (last.mod == Modifier::Circumflex) {
                last.mod = Modifier::None;
                ProcessChar(c);
                // With spell check ON, block further modifiers to prevent
                // oo→ô→oo→ô re-trigger cycle ("oo" is ValidPrefix in spell
                // checker since "oong" is valid, so spellCheckDisabled_ alone
                // doesn't catch it — unlike "aa"/"ee" which are Invalid).
                if (config_.spellCheckEnabled) {
                    escape_.escape(EscapeKind::Circumflex);
                }
                return true;
            }
            // Reject adjacent circumflex when the result is an invalid
            // syllable — catches split tone/mod typos ("của" + 'a' → c,ủ,â)
            // via SpellCheck's tone/mod invariant and structural invalidity
            // ("hò" + 'a' + 'a' → h,ò,â with "âo" not in vowel table).
            if (ShouldRejectModifier(states_.size() - 1,
                                     Modifier::Circumflex, lower)) {
                return false;  // Fall through to ProcessChar — add vowel literally
            }
            // Apply circumflex - PRESERVE FIRST LETTER CASE
            last.mod = Modifier::Circumflex;
            return true;
        }

        // Free marking: backward scan for circumflex across intervening chars
        // e.g., "tieng" + 'e' → "tiêng", "cau" + 'a' → "câu", "chieu" + 'e' → "chiêu"
        // Crosses consonants freely; crosses vowels only with spell-check validation (if enabled)
        if (isCircumflexBase) {
            // GUARD: don't apply cross-vowel circumflex if it completes a contiguous triphthong
            // e.g., "ngoe" + 'o' -> forms "o e o" triphthong, so let it be "ngoeo" instead of "ngôe"
            size_t n = states_.size();
            if (n >= 2 && states_[n - 1].IsVowel() && states_[n - 2].IsVowel() &&
                IsTriphthong(states_[n - 2].base, states_[n - 1].base, lower)) {
                return false; 
            }

            bool needsValidation = false;  // True when crossing different vowels
            int consonantsCrossed = 0;     // Count consonants in path (for same-vowel coda check)
            wchar_t singleCoda = 0;        // Base of single consonant crossed (for coda validation)
            for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
                if (it->IsVowel() && it->base != lower) { needsValidation = true; continue; }
                if (!it->IsVowel()) {
                    if (consonantsCrossed == 0) singleCoda = it->base;
                    ++consonantsCrossed;
                }
                if (it->IsVowel() && it->base == lower) {
                    // Reject cross-vowel if: unsupported modifier
                    // Horn undo (ươ→uô) always allowed across vowels
                    if (needsValidation && it->mod != Modifier::Horn &&
                         (it->mod != Modifier::None && it->mod != Modifier::Circumflex)) break;

                    if (it->mod == Modifier::Circumflex) {
                        it->mod = Modifier::None;
                        ProcessChar(c);
                        escape_.escape(EscapeKind::Circumflex);
                        return true;
                    }
                    if (it->mod == Modifier::Breve && lower == L'a') {
                        it->mod = Modifier::Circumflex;
                        RelocateToneToTarget();
                        return true;
                    }
                    if (it->mod == Modifier::Horn && lower == L'o') {
                        auto oIndex = static_cast<size_t>(states_.rend() - it - 1);
                        it->mod = Modifier::Circumflex;
                        UndoHornU(states_.data(), oIndex);
                        RelocateToneToTarget();
                        return true;
                    }
                    if (it->mod == Modifier::None) {
                        // Spell check ON: validate the RESULT of applying circumflex.
                        //   Rejects "vào" + 'a' → "vầo" (invalid syllable) while
                        //   allowing "cau" + 'a' → "câu" and "chieu" + 'e' → "chiêu".
                        // Spell check OFF, cross-vowel + consonant: always reject.
                        //   Vietnamese circumflex never crosses diff-vowel + consonant
                        //   (dấu mũ ở SAU trong iê/uô → match trước needsValidation;
                        //    dấu mũ ở TRƯỚC trong âu/ây/êu/ôi → không có coda).
                        //   Catches "readme" (e←a←dm←e) and "review" (e←v←i←e).
                        // Spell check OFF, same-vowel: reject if single consonant is not
                        //   a valid Vietnamese coda (c/m/n/p/t). Catches "release" (e→l→e)
                        //   while allowing "hiên" (e→n→e) and "tiêng" (e→ng→e).
                        if (needsValidation) {
                            if (config_.spellCheckEnabled) {
                                size_t targetIdx =
                                    static_cast<size_t>(states_.rend() - it - 1);
                                if (ShouldRejectModifier(targetIdx, Modifier::Circumflex, lower))
                                    break;
                            } else if (consonantsCrossed >= 1) {
                                engProt_.bias = LanguageBias::HardEnglish;
                                break;
                            }
                        } else if (!config_.spellCheckEnabled && consonantsCrossed == 1) {
                            bool validCoda = (singleCoda == L'c' || singleCoda == L'k' ||
                                              singleCoda == L'm' || singleCoda == L'n' ||
                                              singleCoda == L'p' || singleCoda == L't');
                            if (!validCoda) {
                                engProt_.bias = LanguageBias::HardEnglish;
                                break;
                            }
                        } else if (config_.spellCheckEnabled && consonantsCrossed >= 1) {
                            // Same-vowel free-marking across consonants: reject
                            // transformations that produce invalid syllables.
                            // Catches "gacha" → "gâch", "bacha" → "bâch", etc.
                            // — âch/ăch are not valid Vietnamese codas.
                            size_t targetIdx = static_cast<size_t>(states_.rend() - it - 1);
                            if (!WouldBeValidSyllable(targetIdx, Modifier::Circumflex)) break;
                        }
                        it->mod = Modifier::Circumflex;
                        RelocateToneToTarget();
                        return true;
                    }
                    break;
                }
            }
        }
    }

    // Handle dd → đ
    if (lower == L'd') {
        return ProcessDModifier(c);
    }

    return false;
}

//-----------------------------------------------------------------------------
// W-Modifier Processing - EXPLICIT PRIORITY ORDER
//-----------------------------------------------------------------------------

bool TypingEngine::ProcessWModifier(wchar_t c) {
    // Simple Telex: 'w' only acts as modifier when preceded by a/o/u vowel
    if (config_.inputMethod == InputMethod::SimpleTelex) {
        bool hasVowelContext = false;
        for (const auto& s : states_) {
            if (s.IsVowel() && (s.base == L'a' || s.base == L'o' || s.base == L'u')) {
                hasVowelContext = true;
                break;
            }
        }
        if (!hasVowelContext) return false;  // Let PushChar add 'w' as literal
    }

    // Check if 'u' at position i is part of QU consonant cluster
    // QU-cluster 'u' should not be treated as a modifiable vowel
    // Only unmodified 'u' qualifies — a horned ư (e.g., P8 synthetic) is not a cluster 'u'.
    auto isQUClusterU = [this](size_t i) -> bool {
        return i > 0 && states_[i].base == L'u' && states_[i].mod == Modifier::None
            && states_[i - 1].base == L'q';
    };

    // PRIORITY ORDER for 'w':
    // P1: "ua" pattern → apply horn to 'u' (mưa, được)
    // P2: "uo" pair horn cycle — default ươ, edge prefixes (h/th/kh) get 3-state
    // P3: "oa" pattern → apply breve to 'a' (hoặc)
    // P4: Escape - if already have horn/breve, second 'w' clears it
    // P5: Standalone 'u' → horn
    // P6: Standalone 'o' (not in oa/uo) → horn
    // P7: Standalone 'a' → breve

    // Analyze current state (skip QU-cluster 'u' for modification targets)
    bool hasUA = false, hasOA = false, hasUO = false, hasUU = false;
    size_t uIdx = SIZE_MAX, firstUIdx = SIZE_MAX, oIdx = SIZE_MAX, aIdx = SIZE_MAX;
    size_t hornedIdx = SIZE_MAX, brevedIdx = SIZE_MAX;

    for (size_t i = 0; i < states_.size(); ++i) {
        if (!states_[i].IsVowel()) continue;

        // Skip 'u' in QU cluster — it's a consonant, not modifiable
        if (isQUClusterU(i)) continue;

        wchar_t base = states_[i].base;
        Modifier mod = states_[i].mod;

        if (base == L'u') {
            if (mod == Modifier::Horn) hornedIdx = i;
            else if (mod == Modifier::None) {
                if (firstUIdx == SIZE_MAX) firstUIdx = i;  // first unmodified u
                uIdx = i;  // last unmodified u
            }
        } else if (base == L'o') {
            if (mod == Modifier::Horn) hornedIdx = i;
            else if (mod == Modifier::None || mod == Modifier::Circumflex) oIdx = i;
        } else if (base == L'a') {
            if (mod == Modifier::Breve) brevedIdx = i;
            else if (mod == Modifier::None || mod == Modifier::Circumflex) aIdx = i;
        }
    }

    // Detect vowel patterns and uo pair indices (skip QU-cluster 'u')
    size_t pairU = SIZE_MAX, pairO = SIZE_MAX;
    for (size_t i = 0; i + 1 < states_.size(); ++i) {
        if (states_[i].IsVowel() && states_[i+1].IsVowel()) {
            if (isQUClusterU(i)) continue;
            wchar_t first = states_[i].base;
            wchar_t second = states_[i+1].base;
            if (first == L'u' && second == L'a') hasUA = true;
            if (first == L'o' && second == L'a') hasOA = true;
            if (first == L'u' && second == L'o') { hasUO = true; pairU = i; pairO = i + 1; }
            if (first == L'u' && second == L'u') hasUU = true;
        }
    }

    // P1: "ua" pattern → horn on 'u' (mưa, được, thưa)
    if (hasUA && uIdx != SIZE_MAX) {
        states_[uIdx].mod = Modifier::Horn;
        if (aIdx != SIZE_MAX && states_[aIdx].mod == Modifier::Circumflex) {
            states_[aIdx].mod = Modifier::None;
        }
        RelocateToneToHornVowel();
        return true;
    }

    // P2: "uo" pair — horn cycle
    // Non-edge: uo → ươ → uo (two-press cycle).
    // Edge (h/th/kh): uo → uơ → ươ → uo (three-press cycle, uơ first).
    // ApplyAutoUO() will auto-complete uơ → ươ when followed by another char.
    if (hasUO && pairU != SIZE_MAX) {
        Modifier uMod = states_[pairU].mod;
        Modifier oMod = states_[pairO].mod;
        bool isEdge = IsUOEdgeCasePrefix(states_.data(), states_.size(), pairU);

        // Forward (non-edge): uo/uô → ươ (first press, covers circumflex replacement)
        // Forward (edge h/th/kh): uo → uơ (first press — default to uơ for huơ/khuơ)
        if (uMod != Modifier::Horn && (oMod == Modifier::None || oMod == Modifier::Circumflex)) {
            if (isEdge) {
                states_[pairO].mod = Modifier::Horn;
            } else {
                states_[pairU].mod = Modifier::Horn;
                states_[pairO].mod = Modifier::Horn;
            }
            RelocateToneToHornVowel();
            return true;
        }

        // Forward: ưo → ươ (u already horned via P5, now complete pair)
        if (uMod == Modifier::Horn && oMod == Modifier::None) {
            states_[pairO].mod = Modifier::Horn;
            RelocateToneToHornVowel();
            return true;
        }

        // Forward (edge): uơ → ươ (h/th/kh second press — complete the pair)
        if (uMod == Modifier::None && oMod == Modifier::Horn && isEdge) {
            states_[pairU].mod = Modifier::Horn;
            RelocateToneToHornVowel();
            return true;
        }

        // Escape: ươ → uo (third press for h/th/kh, second press for others)
        if (uMod == Modifier::Horn && oMod == Modifier::Horn) {
            states_[pairU].mod = Modifier::None;
            states_[pairO].mod = Modifier::None;
            ProcessChar(c);
            escape_.escape(EscapeKind::Horn);
            return true;
        }

        // Escape: uơ → uo (non h/th/kh, or any remaining uơ state)
        if (uMod == Modifier::None && oMod == Modifier::Horn) {
            states_[pairO].mod = Modifier::None;
            ProcessChar(c);
            escape_.escape(EscapeKind::Horn);
            return true;
        }
    }

    // P3: "oa" pattern → breve on 'a' (hoặc)
    if (hasOA && aIdx != SIZE_MAX) {
        states_[aIdx].mod = Modifier::Breve;
        return true;
    }

    // P4: Escape - clear existing modifier and add 'w' as literal
    // Must be before standalone applications (P5-P7) so that second 'w'
    // escapes the first modification.
    if (hornedIdx != SIZE_MAX) {
        // Special case: P8-synthesized ư (ww → w escape)
        // Only erase when synthetic ư is the last state (immediate ww sequence).
        // If other chars were typed after the synthetic ư (e.g., "window"),
        // it's now part of a word — do regular escape instead.
        if (states_[hornedIdx].synthetic && hornedIdx == states_.size() - 1) {
            // Preserve the case of the first keystroke (the 'W' that produced Ư).
            // Passing the raw second keystroke drops uppercase intent: W + w → w
            // instead of W. Force case on the escape output to match the original.
            bool origUpper = states_[hornedIdx].isUpper;
            states_.erase(states_.begin() + static_cast<ptrdiff_t>(hornedIdx));
            ProcessChar(origUpper ? towupper(c) : towlower(c));
            escape_.escape(EscapeKind::Horn);
            return true;
        }
        UndoHornU(states_.data(), hornedIdx);  // Undo companion 'u' horn BEFORE clearing 'o'
        states_[hornedIdx].mod = Modifier::None;
        ProcessChar(c);
        escape_.escape(EscapeKind::Horn);
        return true;
    }
    if (brevedIdx != SIZE_MAX) {
        states_[brevedIdx].mod = Modifier::None;
        ProcessChar(c);
        escape_.escape(EscapeKind::Breve);
        return true;
    }

    // When a modifier would apply to a vowel with intervening non-vowel state(s)
    // before end of buffer, validate that the resulting syllable is not Invalid.
    // Rejects "gachw" → "găch", "congw" → "cơng", "hunw" → "hưn", etc.
    auto hasNonVowelAfter = [this](size_t idx) {
        for (size_t i = idx + 1; i < states_.size(); ++i) {
            if (!states_[i].IsVowel()) return true;
        }
        return false;
    };

    // P5: Standalone 'u' → horn
    // For "uu" pattern, horn goes on FIRST 'u' (lưu, cưu, hưu): the second 'u' is the glide.
    if (uIdx != SIZE_MAX) {
        size_t targetU = (hasUU && firstUIdx != SIZE_MAX) ? firstUIdx : uIdx;
        // Pass aIdx so the validation matches the real mutation (u→horn and,
        // if present, strip sister â back to a). Example: "uaan" + w.
        if (hasNonVowelAfter(targetU) &&
            !WouldBeValidSyllable(targetU, Modifier::Horn, aIdx)) {
            return false;
        }
        states_[targetU].mod = Modifier::Horn;
        if (aIdx != SIZE_MAX && states_[aIdx].mod == Modifier::Circumflex) {
            states_[aIdx].mod = Modifier::None;
        }
        RelocateToneToHornVowel();
        return true;
    }

    // P6: Standalone 'o' (not in oa pattern) → horn (replaces circumflex: ô→ơ)
    if (oIdx != SIZE_MAX && !hasOA) {
        if (hasNonVowelAfter(oIdx) && !WouldBeValidSyllable(oIdx, Modifier::Horn)) {
            return false;
        }
        states_[oIdx].mod = Modifier::Horn;
        RelocateToneToHornVowel();
        return true;
    }

    // P7: Standalone 'a' → breve (or switch circumflex → breve: â→ă)
    if (aIdx != SIZE_MAX && (states_[aIdx].mod == Modifier::None ||
                              states_[aIdx].mod == Modifier::Circumflex)) {
        if (hasNonVowelAfter(aIdx) && !WouldBeValidSyllable(aIdx, Modifier::Breve)) {
            return false;
        }
        states_[aIdx].mod = Modifier::Breve;
        return true;
    }

    // P8: Full Telex only — standalone 'w' with no modifiable vowel → insert ư
    // Only fires when there are no vowels yet in the buffer (onset consonants or empty),
    // OR when the only vowel is 'i' in a potential "gi" consonant cluster (giữ, giữa, giường).
    // After a non-cluster vowel (e.g., "re" + w), 'w' is treated as literal — no Vietnamese
    // word has a vowel followed by standalone ư via P8.
    // In QU cluster, don't insert standalone ư (let 'w' be literal: "quew" → "quew")
    if (config_.inputMethod != InputMethod::SimpleTelex && !IsInQUCluster()) {
        bool hasNonClusterVowel = false;
        for (size_t i = 0; i < states_.size(); ++i) {
            if (!states_[i].IsVowel()) continue;
            // 'i' after 'g' is a potential "gi" cluster consonant — don't count it
            if (states_[i].base == L'i' && i > 0 && states_[i - 1].base == L'g') continue;
            hasNonClusterVowel = true;
            break;
        }
        if (!hasNonClusterVowel) {
            CharState s;
            s.base = L'u';
            s.mod = Modifier::Horn;
            s.isUpper = iswupper(c);
            s.synthetic = true;  // Mark as P8-synthesized (ww escape removes it entirely)
            s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
            states_.push_back(s);
            return true;
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// D-Modifier Processing (dd → đ)
// Scan logic via FindStrokeDTarget (EngineHelpers.h)
//-----------------------------------------------------------------------------

bool TypingEngine::ProcessDModifier(wchar_t c) {
    if (escape_.isEscaped(EscapeKind::Stroke)) return false;
    size_t dIdx = FindStrokeDTarget(states_.data(), states_.size());
    if (dIdx == SIZE_MAX) return false;

    CharState& target = states_[dIdx];
    if (target.mod == Modifier::None) {
        target.mod = Modifier::Stroke;
        return true;
    } else if (target.mod == Modifier::Stroke) {
        target.mod = Modifier::None;
        escape_.escape(EscapeKind::Stroke);
        ProcessChar(c);
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// QU Cluster Detection
//-----------------------------------------------------------------------------

bool TypingEngine::IsInQUCluster() const {
    if (states_.size() < 2) return false;

    for (size_t i = 0; i + 1 < states_.size(); ++i) {
        if (states_[i].base == L'q' && states_[i+1].base == L'u'
            && states_[i+1].mod == Modifier::None) {
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
// Remove Consumed Raw Entry (for tone escape auto-restore fix)
//-----------------------------------------------------------------------------

void TypingEngine::EraseConsumedRaw(size_t idx) {
    if (idx >= rawInput_.size()) return;
    rawInput_.erase(rawInput_.begin() + static_cast<ptrdiff_t>(idx));
    // Adjust all indices that reference positions after the erased entry
    for (auto& s : states_) {
        if (s.rawIdx > idx) s.rawIdx--;
        if (s.toneRawIdx != SIZE_MAX && s.toneRawIdx > idx) s.toneRawIdx--;
    }
}

//-----------------------------------------------------------------------------
// Character Processing
//-----------------------------------------------------------------------------

void TypingEngine::ProcessChar(wchar_t /*c*/, wchar_t lower, bool isUpper) {
    CharState s;
    s.base = lower;
    s.isUpper = isUpper;
    s.rawIdx = rawInput_.empty() ? 0 : rawInput_.size() - 1;
    states_.push_back(s);
}

//-----------------------------------------------------------------------------
// Auto ươ Transformation — O(1) bounded scan
//-----------------------------------------------------------------------------

void TypingEngine::ApplyAutoUO() {
    // Requires 3+ chars: both patterns need a char after the uo/ưo pair.
    if (states_.size() < 3) return;

    // Scan backward, bounded to last 4 positions
    size_t start = (states_.size() > 4) ? states_.size() - 4 : 0;
    for (size_t i = start; i + 2 < states_.size(); ++i) {
        // Skip QU cluster: 'u' in "qu" is a consonant glide, not a modifiable vowel
        if (i > 0 && states_[i].base == L'u' && states_[i - 1].base == L'q') continue;

        // Pattern 1: u(no horn) + ơ(has horn) → horn the u to complete ươ
        // When followed by another character, always auto-complete uơ → ươ
        // (including h/th/kh prefixes: huơn → hươn, thuở typed as thuowr).
        if (states_[i].base == L'u' && states_[i].mod == Modifier::None &&
            states_[i+1].base == L'o' && states_[i+1].mod == Modifier::Horn) {
            states_[i].mod = Modifier::Horn;
        }

        // Pattern 2: ư(has horn) + o(no horn) → horn the o to complete ươ
        // Tone relocation needed: ư was horned by ApplyW P5 when only 'u' existed,
        // so any tone placed before 'o' arrived landed on ư and must now move to ơ.
        if (states_[i].base == L'u' && states_[i].mod == Modifier::Horn &&
            states_[i+1].base == L'o' && states_[i+1].mod == Modifier::None) {
            states_[i+1].mod = Modifier::Horn;
            RelocateToneToTarget();
        }
    }
}

//-----------------------------------------------------------------------------
// Tone Relocation (after horn applied)
//-----------------------------------------------------------------------------

void TypingEngine::RelocateToneToHornVowel() {
    size_t hornIdx = SIZE_MAX;
    size_t tonedIdx = SIZE_MAX;

    for (size_t i = 0; i < states_.size(); ++i) {
        if (states_[i].IsVowel()) {
            if (states_[i].mod == Modifier::Horn) hornIdx = i;
            if (states_[i].tone != Tone::None) tonedIdx = i;
        }
    }

    if (hornIdx != SIZE_MAX && tonedIdx != SIZE_MAX && hornIdx != tonedIdx) {
        // Allow relocation from unmodified vowels and from horn vowels
        // (handles "ươ" diphthong: tone moves from ư to ơ). Must stay within
        // the same syllable — never relocate across a consonant.
        if ((states_[tonedIdx].mod == Modifier::None ||
             states_[tonedIdx].mod == Modifier::Horn) &&
            !HasConsonantBetween(states_.data(), tonedIdx, hornIdx)) {
            states_[hornIdx].tone = states_[tonedIdx].tone;
            states_[tonedIdx].tone = Tone::None;
        }
    }
}

//-----------------------------------------------------------------------------
// Tone Relocation (after any modifier changes priority)
//-----------------------------------------------------------------------------

void TypingEngine::RelocateToneToTarget() {
    // Find where the tone currently is
    size_t tonedIdx = FindTonedVowelIndex(states_.data(), states_.size());
    if (tonedIdx == SIZE_MAX) return;

    // Find where the tone should be now (modifier may have changed priority)
    size_t targetIdx = FindToneTarget();
    if (targetIdx == SIZE_MAX || targetIdx == tonedIdx) return;

    // Never relocate across a consonant — Vietnamese syllables keep all
    // vowels contiguous, so a consonant between tonedIdx and targetIdx
    // means they belong to different syllables.
    if (HasConsonantBetween(states_.data(), tonedIdx, targetIdx)) return;

    if (IsToneRelocBlockedByP4(states_.data(), states_.size(), targetIdx, config_.modernOrtho))
        return;

    states_[targetIdx].tone = states_[tonedIdx].tone;
    states_[targetIdx].toneRawIdx = states_[tonedIdx].toneRawIdx;
    states_[tonedIdx].tone = Tone::None;
    states_[tonedIdx].toneRawIdx = SIZE_MAX;
}

//-----------------------------------------------------------------------------
// Tone Target Finding — stack-allocated, no heap alloc
//-----------------------------------------------------------------------------

size_t TypingEngine::FindToneTarget() const {
    return config_.modernOrtho ? FindToneTargetModern() : FindToneTargetClassic();
}

size_t TypingEngine::FindToneTargetClassic() const {
    return FindToneTargetImpl(kDiphthongClassic, false);
}

size_t TypingEngine::FindToneTargetModern() const {
    return FindToneTargetImpl(kDiphthongModern, true);
}

size_t TypingEngine::FindToneTargetImpl(const uint8_t table[6][6], bool checkTriphthongs) const {
    return NextKey::FindToneTargetImpl(states_.data(), states_.size(), table, checkTriphthongs);
}

//-----------------------------------------------------------------------------
// Composition (State → Unicode) — O(1) flat array lookups
//-----------------------------------------------------------------------------

wchar_t TypingEngine::Compose(const CharState& s) {
    if (s.IsEmpty()) return 0;

    wchar_t ch = s.base;

    // Special: đ
    if (s.IsD() && s.mod == Modifier::Stroke) {
        return s.isUpper ? L'Đ' : L'đ';
    }

    // Step 1: Apply modifier — O(1) array lookup
    if (s.mod != Modifier::None && s.IsVowel()) {
        int bi = VowelBaseIndex(s.base);
        int mi = ModifierIndex(s.mod);
        if (bi >= 0 && mi >= 0) {
            wchar_t modified = kModifiedVowel[bi][mi];
            if (modified) ch = modified;
        }
    }

    // Step 2: Apply tone — O(1) array lookup
    if (s.tone != Tone::None) {
        int ti = ToneBaseIndex(ch);
        int si = ToneIndex(s.tone);
        if (ti >= 0 && si >= 0) {
            ch = kTonedVowel[ti][si];
        }
    }

    // Step 3: Apply case
    if (s.isUpper) {
        ch = ToUpperVietnamese(ch);
    }

    return ch;
}

const std::wstring& TypingEngine::ComposeAll() const {
    composeBuf_.clear();
    for (const auto& s : states_) {
        wchar_t ch = Compose(s);
        if (ch != 0) composeBuf_ += ch;
    }
    return composeBuf_;
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

void TypingEngine::Backspace() {
    if (states_.empty()) return;
    escape_.clear();  // Allow đ re-trigger + Vietnamese re-trigger after user edits

    // Undo quick start consonant: ph→f, gi→j, qu→w (collapse both chars to original)
    if (quickStartKey_ != 0 && states_.size() == 2) {
        states_.clear();
        rawInput_.clear();
        rawInput_.push_back(quickStartKey_);
        ProcessChar(quickStartKey_);
        quickStartKey_ = 0;
        UpdateSpellState();
        return;
    }
    quickStartKey_ = 0;

    // Undo quick consonant expansion — restore original key in-place.
    // e.g., "aph" + BS → "app" (not "ap"), "rieng" + BS → "rienn".
    // The triggering key is still in rawInput_; re-add it as an escaped literal
    // so the user sees the full original sequence without having to retype.
    if (qc_.hasActive() && states_.size() - 1 == qc_.resultIndex) {
        // For uu→ươ: undo the horn on the preceding 'u' before popping ơ
        UndoHornU(states_.data(), states_.size() - 1);

        wchar_t originalKey = qc_.lastKey;
        qc_.markEscaped();  // escaped=true, clearActive (idx+lastKey)

        states_.pop_back();  // Remove the converted char (h in ph, g in ng, i in gi, ơ in ươ)
        // rawInput_ already contains the original triggering key — don't trim it.
        // Re-add it as a literal char; qc_.escaped prevents re-conversion.
        if (originalKey != 0) {
            ProcessChar(originalKey);
        }

        UpdateSpellState();
        RecalcEnglishBias(states_.data(), states_.size(), engProt_);
        if (IsTelexMode()) CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
        return;
    }
    qc_.clearActive();

    // Trim rawInput_ to the position when this state was created.
    // This correctly handles modifier keys (circumflex, tone) that add to rawInput_
    // without creating new states — backspace removes all associated raw entries.
    size_t rawTarget = states_.back().rawIdx;
    states_.pop_back();
    if (rawInput_.size() > rawTarget) {
        rawInput_.resize(rawTarget);
    }
    UpdateSpellState();

    // English Protection: recalculate bias after backspace
    RecalcEnglishBias(states_.data(), states_.size(), engProt_);
    if (IsTelexMode()) CheckZwjfInitialBias(states_.data(), states_.size(), config_, engProt_);
}

const std::wstring& TypingEngine::Peek() const {
    return ComposeAll();
}

std::wstring TypingEngine::Commit() {
    std::wstring composed = ComposeAll();

    // Single quick-start consonant alone (f->ph, j->gi, w->qu) — always restore
    // This allows "j " -> "j " instead of "gi ", since "gi" is a valid word and wouldn't auto-restore naturally.
    if (quickStartKey_ != 0 && rawInput_.size() == 1) {
        std::wstring raw(rawInput_.begin(), rawInput_.end());
        if (ShouldAutoRestore(raw, composed)) {
            Reset();
            return raw;
        }
    }

    // Quick consonant alone (gg, uu) — always restore regardless of spell check setting
    if (qc_.onlyQC) {
        std::wstring raw(rawInput_.begin(), rawInput_.end());
        if (ShouldAutoRestore(raw, composed)) {
            Reset();
            return raw;
        }
    }

    // Guard: skip auto-restore when any of these are true:
    //  - spell check / auto-restore not enabled
    //  - an active quick consonant (nn→ng, cc→ch, etc.) was applied — user did
    //    this intentionally; restoring would undo the conversion they wanted.
    //    (qc_.hasActive() when quick consonant is active)
    bool skipAutoRestore = !config_.spellCheckEnabled
                        || !config_.autoRestoreEnabled
                        || qc_.hasActive();
    if (!skipAutoRestore) {
        bool shouldRestore = spellCheckDisabled_;  // Already known Invalid

        // Also restore ValidPrefix at commit time — incomplete words like "úẻ"
        // (from typing "user") are ValidPrefix during typing (allowing future
        // modifiers) but should auto-restore when the user commits.
        if (!shouldRestore && !states_.empty()) {
            auto result = SpellCheck::Validate(states_.data(), states_.size(), config_.allowZwjf);
            shouldRestore = (result == SpellCheck::Result::ValidPrefix);
        }

        if (shouldRestore) {
            // Spell exclusion list takes priority. HasIntentionalStrokeD heuristic
            // also protects abbreviations like đt, đh while restoring đwa, ăndd.
            bool excluded = IsSpellExcluded(states_.data(), states_.size(),
                                            config_.spellExclusions,
                                            [](const CharState& s) { return Compose(s); });
            bool keepComposed = excluded ||
                                HasIntentionalStrokeD(rawInput_, composed);
            if (!keepComposed) {
                std::wstring raw(rawInput_.begin(), rawInput_.end());
                if (ShouldAutoRestore(raw, composed)) {
                    Reset();
                    return raw;
                }
            }
        }
    }

    Reset();
    return composed;
}

void TypingEngine::Reset() {
    states_.clear();
    rawInput_.clear();
    spellCheckDisabled_ = false;
    qc_.Reset();
    escape_.clear();
    quickStartKey_ = 0;
    engProt_.Reset();
}

bool TypingEngine::SeedFromText(const std::wstring& text) {
    Reset();
    states_.reserve(text.size());
    rawInput_.reserve(text.size());
    for (wchar_t ch : text) {
        wchar_t base = 0;
        int modIdx = -1, toneIdx = -1;
        bool isUpper = false;
        if (!DecomposeVietChar(ch, base, modIdx, toneIdx, isUpper)) {
            Reset();
            return false;
        }
        CharState st;
        st.base = base;
        st.isUpper = isUpper;
        switch (modIdx) {
            case 0: st.mod = Modifier::Circumflex; break;
            case 1: st.mod = Modifier::Breve; break;
            case 2: st.mod = Modifier::Horn; break;
            case 3: st.mod = Modifier::Stroke; break;
            default: st.mod = Modifier::None; break;
        }
        switch (toneIdx) {
            case 0: st.tone = Tone::Acute; break;
            case 1: st.tone = Tone::Grave; break;
            case 2: st.tone = Tone::Hook; break;
            case 3: st.tone = Tone::Tilde; break;
            case 4: st.tone = Tone::Dot; break;
            default: st.tone = Tone::None; break;
        }
        // Synthetic raw: only the base letter — we don't have original keystrokes.
        // This is enough for Backspace/PushChar to work correctly after seeding.
        st.rawIdx = rawInput_.size();
        st.toneRawIdx = SIZE_MAX;
        rawInput_.push_back(base);
        states_.push_back(st);
    }
    UpdateSpellState();
    // Populate engProt_.bias so IsEnglishWord() can report on seeded text.
    CheckEnglishBias(states_.data(), states_.size(), engProt_);
    return true;
}



//-----------------------------------------------------------------------------
// Spell Exclusion — modifier key match
//-----------------------------------------------------------------------------

bool TypingEngine::WouldModifierKeyMatchExclusion(wchar_t lower) const {
    auto compose = [](const CharState& s) { return Compose(s); };
    // Telex modifier keys
    if (lower == L'd') {
        return WouldStrokeDMatchExclusion(states_.data(), states_.size(),
            config_.spellExclusions, compose);
    }
    if (lower == L'w') {
        return WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
            config_.spellExclusions, compose, Modifier::Horn, L"uo") ||
            WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
            config_.spellExclusions, compose, Modifier::Breve, L"a");
    }
    if (lower == L'a' || lower == L'e' || lower == L'o') {
        return WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
            config_.spellExclusions, compose, Modifier::Circumflex, L"aeo");
    }
    // VNI modifier keys
    if (lower == L'6') {
        return WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
            config_.spellExclusions, compose, Modifier::Circumflex, L"aeo");
    }
    if (lower == L'7') {
        return WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
            config_.spellExclusions, compose, Modifier::Horn, L"uo");
    }
    if (lower == L'8') {
        return WouldAnyModifierMatchExclusion(states_.data(), states_.size(),
            config_.spellExclusions, compose, Modifier::Breve, L"a");
    }
    if (lower == L'9') {
        return WouldStrokeDMatchExclusion(states_.data(), states_.size(),
            config_.spellExclusions, compose);
    }
    return false;
}

//-----------------------------------------------------------------------------
// VNI Modifier Processing (keys 6, 7, 8, 9)
//-----------------------------------------------------------------------------

bool TypingEngine::ProcessVniModifier(wchar_t c) {
    if (c == L'9') return ProcessDModifier(c);
    if (c == L'7') return ProcessVniHornModifier(c);

    Modifier targetMod = Modifier::None;
    if (c == L'6') targetMod = Modifier::Circumflex;
    else if (c == L'8') targetMod = Modifier::Breve;
    else return false;

    return ProcessVniVowelModifier(targetMod, c);
}

bool TypingEngine::ProcessVniHornModifier(wchar_t c) {
    if (states_.empty()) return false;

    // --- uo pair cycle (same logic as Telex ProcessWModifier P2) ---
    size_t uIdx = SIZE_MAX, oIdx = SIZE_MAX;
    for (size_t i = 0; i < states_.size(); ++i) {
        if (!states_[i].IsVowel()) continue;
        if (IsClusterConsonant(states_.data(), states_.size(), i)) continue;
        if (states_[i].base == L'u') uIdx = i;
        if (states_[i].base == L'o') oIdx = i;
    }

    // uo pair detected (adjacent)
    if (uIdx != SIZE_MAX && oIdx != SIZE_MAX && oIdx == uIdx + 1) {
        bool isEdge = IsUOEdgeCasePrefix(states_.data(), states_.size(), uIdx);
        bool uHorn = states_[uIdx].IsHorn();
        bool oHorn = states_[oIdx].IsHorn();

        if (!uHorn && !oHorn) {
            // uo → apply horn
            if (isEdge) {
                states_[oIdx].mod = Modifier::Horn;  // Edge: horn o first
            } else {
                states_[uIdx].mod = Modifier::Horn;
                states_[oIdx].mod = Modifier::Horn;
            }
            RelocateToneToTarget();
            return true;
        }
        // Forward: ưo → ươ (u already horned via explicit hu7, complete pair)
        if (uHorn && !oHorn) {
            states_[oIdx].mod = Modifier::Horn;
            RelocateToneToTarget();
            return true;
        }
        if (isEdge && !uHorn && oHorn) {
            // Edge step 2: uơ → ươ (horn u too)
            states_[uIdx].mod = Modifier::Horn;
            RelocateToneToTarget();
            return true;
        }
        // Escape: both horned — clear pair
        // (isEdge && uHorn && !oHorn already handled above by ưo→ươ forward)
        if (uHorn && oHorn) {
            states_[uIdx].mod = Modifier::None;
            states_[oIdx].mod = Modifier::None;
            ProcessChar(c);
            escape_.escape(EscapeKind::Horn);
            return true;
        }
    }

    // --- uu pattern (horn first u, like Telex P5 — 2-press cycle) ---
    size_t firstU = SIZE_MAX, lastU = SIZE_MAX;
    int uCount = 0;
    for (size_t i = 0; i < states_.size(); ++i) {
        if (states_[i].base == L'u' && states_[i].IsVowel()) {
            if (IsClusterConsonant(states_.data(), states_.size(), i)) continue;
            if (firstU == SIZE_MAX) firstU = i;
            lastU = i;
            ++uCount;
        }
    }
    if (uCount >= 2 && firstU != lastU) {
        if (states_[firstU].mod == Modifier::None && states_[lastU].mod == Modifier::None) {
            // Apply: horn first u (lưu, cưu, hưu)
            states_[firstU].mod = Modifier::Horn;
            RelocateToneToTarget();
            return true;
        }
        if (states_[firstU].mod == Modifier::Horn) {
            // Escape: clear horn on first u, add '7' literal (2-press cycle, same as Telex P4)
            states_[firstU].mod = Modifier::None;
            ProcessChar(c);
            escape_.escape(EscapeKind::Horn);
            return true;
        }
    }

    // --- Generic: rightmost eligible o/u → apply horn ---
    return ProcessVniVowelModifier(Modifier::Horn, c);
}

bool TypingEngine::ProcessVniVowelModifier(Modifier targetMod, wchar_t key) {
    if (states_.empty()) return false;

    // Eligible base vowels for each modifier type
    auto isEligible = [targetMod](wchar_t base) -> bool {
        switch (targetMod) {
            case Modifier::Circumflex: return base == L'a' || base == L'e' || base == L'o';
            case Modifier::Horn:       return base == L'o' || base == L'u';
            case Modifier::Breve:      return base == L'a';
            default:                   return false;
        }
    };

    // Pass 1: rightmost unmodified eligible vowel → apply.
    // Reject when the result would be an invalid syllable — catches both
    // split tone/mod ("của" + '6' → c,ủ,â) and structural invalidity
    // ("báo" + '6' → b,a,ô with "aô" not in vowel table). Do NOT fall back
    // to an earlier vowel; split applications on earlier vowels would still
    // be invalid.
    for (size_t i = states_.size(); i-- > 0;) {
        if (!states_[i].IsVowel() || !isEligible(states_[i].base)) continue;
        if (IsClusterConsonant(states_.data(), states_.size(), i)) continue;
        if (states_[i].mod == Modifier::None) {
            if (ShouldRejectModifier(i, targetMod, key)) return false;
            states_[i].mod = targetMod;
            RelocateToneToTarget();
            return true;
        }
    }

    // Pass 1.5: modifier switching on compatible vowels
    // â+8→ă, ă+6→â, ô+7→ơ, ơ+6→ô
    for (size_t i = states_.size(); i-- > 0;) {
        if (!states_[i].IsVowel() || !states_[i].HasModifier()) continue;
        if (IsClusterConsonant(states_.data(), states_.size(), i)) continue;
        wchar_t base = states_[i].base;
        Modifier curMod = states_[i].mod;
        bool canSwitch = false;
        // a: Circumflex ↔ Breve
        if (base == L'a' && ((curMod == Modifier::Circumflex && targetMod == Modifier::Breve) ||
                             (curMod == Modifier::Breve && targetMod == Modifier::Circumflex))) {
            canSwitch = true;
        }
        // o: Circumflex ↔ Horn
        if (base == L'o' && ((curMod == Modifier::Circumflex && targetMod == Modifier::Horn) ||
                             (curMod == Modifier::Horn && targetMod == Modifier::Circumflex))) {
            canSwitch = true;
        }
        if (canSwitch) {
            UndoHornU(states_.data(), i);
            states_[i].mod = targetMod;
            RelocateToneToTarget();
            return true;
        }
    }

    // Pass 2: escape — rightmost vowel with matching modifier → clear
    for (size_t i = states_.size(); i-- > 0;) {
        if (!states_[i].IsVowel()) continue;
        if (IsClusterConsonant(states_.data(), states_.size(), i)) continue;
        if (states_[i].mod == targetMod) {
            UndoHornU(states_.data(), i);
            states_[i].mod = Modifier::None;
            ProcessChar(key);
            escape_.escape(EscapeKind::Modifier);
            return true;
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// Spell Check State Update
//-----------------------------------------------------------------------------

void TypingEngine::UpdateSpellState() {
    UpdateSpellCheck(states_.data(), states_.size(), config_, spellCheckDisabled_,
                     [](const CharState& s) { return Compose(s); });
}

bool TypingEngine::ShouldRejectModifier(size_t targetIdx, Modifier newMod,
                                        wchar_t key) {
    if (!config_.spellCheckEnabled) return false;
    return !WouldBeValidSyllable(targetIdx, newMod) &&
           !WouldModifierKeyMatchExclusion(key);
}

bool TypingEngine::WouldBeValidSyllable(size_t targetIdx, Modifier newMod,
                                        size_t clearCircumflexIdx) {
    if (!config_.spellCheckEnabled) return true;
    if (targetIdx >= states_.size()) return true;
    Modifier saved = states_[targetIdx].mod;
    states_[targetIdx].mod = newMod;
    bool didClear = false;
    if (clearCircumflexIdx < states_.size() &&
        states_[clearCircumflexIdx].mod == Modifier::Circumflex) {
        states_[clearCircumflexIdx].mod = Modifier::None;
        didClear = true;
    }
    auto result = SpellCheck::Validate(states_.data(), states_.size(), config_.allowZwjf);
    if (didClear) states_[clearCircumflexIdx].mod = Modifier::Circumflex;
    states_[targetIdx].mod = saved;
    return result != SpellCheck::Result::Invalid;
}

}  // namespace NextKey
