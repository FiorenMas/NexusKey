# Multi-word Macro Keys (Phrase Macros)

## Problem

Macro keys containing spaces never fire. Example from a real user config:

```toml
'oc om bok' = 'Óoc Om Bok'
```

Typing `oc om bok` + trigger produces nothing. Root cause: `ClearWordState()` (`HookEngine.cpp:1312`) clears `rawMacroBuffer_` on every commit, and space triggers always hit the commit path. The buffer can never accumulate across words.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Trigger for space preservation | Prefix match against stored space-keys | Zero false-positives — buffer only survives when it plausibly matches a user-defined key |
| Space-key index | `std::unordered_set<std::wstring>` populated at load | O(n×k) scan per space commit; n typically < 5, k < 50 |
| Case handling | Follows existing rule: stored-all-lower → case-insensitive; stored-any-upper → exact | Consistent with `docs/macro-case-rules.md` |
| Terminating trigger | Any existing commit trigger (space/period/enter/tab) | Uses current P1/P2 two-step lookup, no matching-logic changes |
| Buffer reset on focus/mouse/arrow | Unchanged — existing `ResetComposition`/`ClearWordState` already covers | — |
| Commit-undo interaction | Clear `macroCrossCommit_` when entering `CommitUndoState::Primed` | Phrase state becomes stale once user starts BS-replay |
| Migration | None | Existing TOML with space-keys just starts working |

## Changes

### 1. `HookEngine.h`

**[ADD]** private member:
```cpp
std::unordered_set<std::wstring> spaceMacroKeys_;  // keys in macroTable_ that contain ' '
```

**[ADD]** private helper declaration:
```cpp
[[nodiscard]] bool IsSpaceMacroPrefix(const std::wstring& candidate) const noexcept;
```

### 2. `HookEngine.cpp`

**[MODIFY]** `ReloadMacroTable()` — populate the index:
```cpp
void HookEngine::ReloadMacroTable() {
    macroTable_ = ConfigManager::LoadMacros(ConfigManager::GetConfigPath());
    spaceMacroKeys_.clear();
    for (const auto& [key, _] : macroTable_) {
        if (key.find(L' ') != std::wstring::npos) spaceMacroKeys_.insert(key);
    }
}
```

**[ADD]** helper (near other macro helpers, locale-aware case check consistent with `TryExpandMacro`):
```cpp
bool HookEngine::IsSpaceMacroPrefix(const std::wstring& candidate) const noexcept {
    if (spaceMacroKeys_.empty()) return false;  // hot-path escape
    std::wstring candLower;  // lazily built
    for (const auto& key : spaceMacroKeys_) {
        if (key.size() < candidate.size()) continue;
        // Stored-case check matches the matching rule in TryExpandMacro:
        // key has any upper → exact prefix; key all lower → case-insensitive prefix.
        std::wstring keyLower = key;
        CharLowerBuffW(keyLower.data(), static_cast<DWORD>(keyLower.size()));
        const bool keyAllLower = (keyLower == key);
        if (keyAllLower) {
            if (candLower.empty()) candLower = ToLowerAscii(candidate);
            if (std::wcsncmp(key.c_str(), candLower.c_str(), candLower.size()) == 0) return true;
        } else {
            if (std::wcsncmp(key.c_str(), candidate.c_str(), candidate.size()) == 0) return true;
        }
    }
    return false;
}
```

**[MODIFY]** save-before-commit site (~line 961-972), extend trigger check:
```cpp
std::wstring savedMacroBuffer;
if (macroEnabled_ && !macroTable_.empty() && !tempMacroOff_ && !rawMacroBuffer_.empty()) {
    wchar_t ch = VkToMacroChar(vkCode);
    if (ch > L' ') {
        savedMacroBuffer = rawMacroBuffer_;  // unchanged
    } else if (ch == L' ' && IsSpaceMacroPrefix(rawMacroBuffer_ + L' ')) {
        savedMacroBuffer = rawMacroBuffer_ + L' ';  // NEW: phrase-prefix preservation
    }
}
```

**[MODIFY]** commit-undo `Primed` entry site — add one line:
```cpp
macroCrossCommit_ = false;  // phrase state is stale once replay begins
```

### 3. `docs/macro-case-rules.md`

Remove the "Macro keys containing spaces do not work" paragraph under **Known limitations**. Add under **Practical patterns**:

```
**Multi-word keys**
"oc om bok" = "Óoc Om Bok"
"hi there" = "Hello there!"
```

Keys containing spaces accumulate across word commits. The buffer only survives a space when the typed prefix matches (case-aware) a stored space-containing key; any non-matching prefix clears the buffer and treats the space as a normal word commit.

## Testing

Extract `IsSpaceMacroPrefix` pure logic into a free function in a new header `src/app/system/MacroPrefix.h` (no Windows deps — just `<string>`, `<unordered_set>`, and an injected case-fold functor). Unit tests in `tests/MacroPrefixTest.cpp`:

| Case | Keys | Candidate | Expected |
|---|---|---|---|
| Empty set | {} | `"oc "` | false |
| Non-prefix | {`"hi there"`} | `"oc "` | false |
| Valid prefix, lower stored | {`"oc om bok"`} | `"oc "` | true |
| Valid prefix, lower stored, upper typed | {`"oc om bok"`} | `"OC "` | true |
| Upper stored, lower typed | {`"Oc Om Bok"`} | `"oc "` | false |
| Upper stored, exact typed | {`"Oc Om Bok"`} | `"Oc "` | true |
| Mid-char mismatch | {`"oc om bok"`} | `"oc omm "` | false |
| Self (full key, not prefix) | {`"oc om bok"`} | `"oc om bok"` | true (prefix of self — intentional; caller gates separately) |

Windows-only integration check: type `oc om bok` + space with the user's actual config → output `Óoc Om Bok`.

## Risk

- **Low.** Gated by `spaceMacroKeys_.empty()` early-return — zero cost for users without space-keys.
- Behavior change: accidental typo matches (`oc om b` + space continuing to accumulate because `oc om bok` is a prefix). Recovers on next non-matching space, no data loss.
- No SharedState, no config migration, no new toggles.

## Out of scope

- Auto-restore of non-macro phrases typed close to a phrase key (e.g., `"oc om peach"` shouldn't undo after partial accumulation — current design clears on non-prefix and that's fine).
- Phrase macros spanning Vietnamese compositions — Vietnamese word in the middle breaks the prefix on the next space, which is correct.
- Backspace into a committed phrase word to re-edit — commit-undo path stays single-word, phrase state clears on entering Primed.
