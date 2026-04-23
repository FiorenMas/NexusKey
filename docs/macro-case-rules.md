# Macro Case-Matching Rules

How NexusKey decides whether a typed string fires a macro, and how the expansion is cased on output.

## Summary

Two rules, both derived entirely from what's written in the TOML config:

1. **The stored key's case** decides whether matching is flexible or strict.
2. **The stored expansion's case**, together with `auto_caps_macro`, decides whether the output adapts to the typed case.

No global mode switch. What you write in `config.toml` is the contract.

## Rule 1 — Matching

| Stored key | Matching behavior | Example |
|---|---|---|
| All lowercase (`btw`, `vaccine`, `oc om bok`) | **Flexible** — matches any case the user types | `btw`, `Btw`, `BTW` all fire `btw = "by the way"` |
| Contains any uppercase (`Chol`, `BTW`, `Đông`) | **Strict** — matches only the exact case | Only `Chol` fires `Chol = "Chôl"`; `chol` and `CHOL` do nothing |

Why: if you went to the trouble of capitalizing a key, that's a deliberate signal you want it to fire only that way. Lowercase keys are the lazy default and stay lenient.

Both forms can coexist:

```toml
chol = "chôl"   # fires on chol / Chol / CHOL
Chol = "Chôl"   # fires only on Chol (exact)
```

## Rule 2 — Auto-caps on the expansion

Controlled by the `auto_caps_macro` feature flag in `[features]`. When **off**, the expansion always goes out exactly as stored.

When **on**, the expansion adapts to the typed case — but **only** when:

- the match was flexible (the stored key is all lowercase), **and**
- the stored expansion is itself all lowercase.

If either side has any uppercase letter, the expansion is emitted verbatim. The moment you write casing into either the key or the expansion, auto-caps steps out of the way.

### Truth table

Stored `chol = "chôl"`, `auto_caps_macro = true`:

| Typed | Output | Why |
|---|---|---|
| `chol` | `chôl` | as stored |
| `Chol` | `Chôl` | first-letter upper follows typed case |
| `CHOL` | `CHÔL` | all-upper follows typed case (locale-aware — `ô` → `Ô`) |
| `ChOl` | `chôl` | mixed/unclear pattern → no transform |

Stored `chol = "Chôl"`, `auto_caps_macro = true`:

| Typed | Output | Why |
|---|---|---|
| `chol` / `Chol` / `CHOL` | `Chôl` | expansion has uppercase → no transform, emit as stored |

Stored `Chol = "Chôl"`, `auto_caps_macro = true` (strict key):

| Typed | Output | Why |
|---|---|---|
| `Chol` | `Chôl` | exact match, no transform |
| `chol` / `CHOL` | *(no match)* | strict key requires exact case |

## Rule 3 — Composition-based matches

When a macro key matches the engine's composed Vietnamese output (e.g. typing `urrl\` in Telex composes `url\` which matches a stored macro `url\`), matching is case-insensitive only — stored case does not enforce strict matching on this path, and auto-caps does not fire. This is deliberate: the "typed" text here is engine output, not raw keystrokes, so case intent is ambiguous. Rare path; rarely relevant.

## Migration note

Before version 2.1.x, keys with any uppercase letter **never fired at all** (a lookup bug). If you had `Chol = "Chôl"` you likely worked around it by lowercasing the key. After the fix:

- Old lowercase workarounds keep working unchanged.
- Uppercase keys now fire correctly, but **only on exact case**. If you relied on an uppercase key accidentally matching typed lowercase text, that no longer happens (it was never supposed to).

If you want both exact and flexible behavior for the same shortcut, add both entries as shown in Rule 1.

## Practical patterns

**Lazy chat shortcuts — lowercase everything**
```toml
btw = "by the way"
omw = "on my way"
ty = "thank you"
```
With `auto_caps_macro = true`, these adapt to sentence-start capitalization automatically.

**Proper nouns / abbreviations — capitalize the key**
```toml
Chol = "Chôl"
Thmay = "Thmây"
API = "Application Programming Interface"
```
Only fires on the exact stored case — won't accidentally trigger mid-sentence.

**Symbol keys (shifted punctuation supported)**
```toml
"->" = "→"
"<3" = "❤"
```
`>`, `<`, `?`, `:`, `"`, `{`, `}`, `|`, `!`, `@`, `#`, etc. are recognized correctly — NexusKey uses `ToUnicodeEx` with the foreground window's keyboard layout, so the actual typed character (honoring Shift/Caps/AltGr) lands in the macro buffer.

Non-letter chars are never uppercase, so symbol keys are treated as lowercase (flexible matching). Auto-caps does nothing because the expansion itself has no letters to transform.

**Multi-word (phrase) keys**
```toml
"oc om bok" = "Óoc Om Bok"
"hi there" = "Hello there!"
```
Keys containing spaces accumulate across word commits. The buffer only survives a space when the typed prefix matches (case-aware, same rule as single-word matching) a stored space-containing key; any non-matching prefix clears the buffer and the space becomes a normal word commit. No overhead for users who don't define any space-containing keys.

**Explicit uppercase variants side-by-side**
```toml
vaccine = "vắc-xin"
VACCINE = "VẮC-XIN"
```
Typing `vaccine`, `Vaccine`, `VACCINE` all fire the lowercase entry (flexible); typing only `VACCINE` exactly would match the strict entry first. Use this when `auto_caps_macro` doesn't give the casing you want.
