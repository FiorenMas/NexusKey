// Shared hotkey capture component — single source for HotkeysDialog and
// ConvertToolDialog. Owns the document-level ^keydown listener, the
// event.code → Win32 VK mapping, the VK→friendly-name table, and the
// double-tap detection state.
//
// Loaded via plain `<script src="../shared/hotkey-capture.js"></script>`
// (Sciter file-copy in Debug, packfolder bundle in Release). Exposes a
// single global: `window.NextKeyHotkeyCapture`.

(function (global) {
    "use strict";

    // ───────────────────── Constants (Win32 VK + bitmask) ─────────────────
    var VK = {
        ESC: 0x1B, TAB: 0x09, SPACE: 0x20, ENTER: 0x0D, BACK: 0x08,
        SHIFT: 0x10, CTRL: 0x11, ALT: 0x12, LWIN: 0x5B, RWIN: 0x5C,
    };
    var MOD = { CTRL: 0x01, SHIFT: 0x02, ALT: 0x04, WIN: 0x08 };
    var DOUBLE_TAP_WINDOW_MS = 400;

    // Sciter delivers `event.keyCode` in its own (GLFW-derived) scheme. We
    // need Win32 VK for the C++ side, so we map from `event.code` (DOM L3,
    // e.g. "KeyA", "F1", "Escape"). See:
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
    var CODE_TO_VK = {
        "Escape":        0x1B, "Tab":           0x09, "Space":         0x20,
        "Enter":         0x0D, "NumpadEnter":   0x0D, "Backspace":     0x08,
        "Delete":        0x2E, "Insert":        0x2D,
        "Home":          0x24, "End":           0x23,
        "PageUp":        0x21, "PageDown":      0x22,
        "ArrowLeft":     0x25, "ArrowUp":       0x26,
        "ArrowRight":    0x27, "ArrowDown":     0x28,
        "CapsLock":      0x14, "PrintScreen":   0x2C, "Pause":         0x13,
        "ContextMenu":   0x5D,
        // Modifiers — captured so isModifierVk can classify them; the
        // capture loop decides whether bare modifier presses commit.
        "ShiftLeft":     0x10, "ShiftRight":    0x10,
        "ControlLeft":   0x11, "ControlRight":  0x11,
        "AltLeft":       0x12, "AltRight":      0x12,
        "MetaLeft":      0x5B, "MetaRight":     0x5C,
        "OSLeft":        0x5B, "OSRight":       0x5C,
        // OEM punctuation — common rebind candidates.
        "Semicolon":     0xBA, "Equal":         0xBB, "Comma":         0xBC,
        "Minus":         0xBD, "Period":        0xBE, "Slash":         0xBF,
        "Backquote":     0xC0, "BracketLeft":   0xDB, "Backslash":     0xDC,
        "BracketRight":  0xDD, "Quote":         0xDE,
    };

    // Shared canonical VK→name table. C++ side calls
    // NextKeyHotkeyCapture.setVkNames(pairs) once at dialog init to keep
    // labels in sync with the C++ FormatHotkeyLabel implementation.
    var VK_NAMES = {};

    // ───────────────────── Pure helpers (exposed) ─────────────────────────
    function codeToVk(code) {
        if (!code) return 0;
        if (CODE_TO_VK[code]) return CODE_TO_VK[code];
        var m = /^(Key|Digit|Numpad|F)([A-Z]|\d+)$/.exec(code);
        if (!m) return 0;
        var suffix = m[2];
        switch (m[1]) {
            case "Key":    return suffix.charCodeAt(0);              // A-Z   → 0x41..0x5A
            case "Digit":  return suffix.charCodeAt(0);              // 0-9   → 0x30..0x39
            case "Numpad": return 0x60 + parseInt(suffix, 10);       // 0-9   → VK_NUMPAD0..9
            case "F":      var n = parseInt(suffix, 10);
                           return (n >= 1 && n <= 24) ? 0x6F + n : 0; // F1..F24
        }
        return 0;
    }

    function vkName(vk) {
        if (VK_NAMES[vk]) return VK_NAMES[vk];
        if (vk >= 0x60 && vk <= 0x69) return "Num" + (vk - 0x60);
        if (vk >= 0x70 && vk <= 0x87) return "F" + (vk - 0x6F);
        if ((vk >= 0x30 && vk <= 0x39) || (vk >= 0x41 && vk <= 0x5A)) {
            return String.fromCharCode(vk);
        }
        // Modifier-as-key — needed when a chord captured the modifier itself
        // (e.g. {vk=VK_SHIFT, mods=Ctrl} for the "Ctrl+Shift" combo). Without
        // these, the label falls back to "VK_16" and looks broken.
        switch (vk) {
            case 0x10: return "Shift";
            case 0x11: return "Ctrl";
            case 0x12: return "Alt";
            case 0x5B: case 0x5C: return "Win";
        }
        return "VK_" + vk;
    }

    function formatLabel(vk, mods, doubleTap) {
        if (doubleTap) return "2×" + vkName(vk);  // 2× implies mods=0
        var parts = [];
        if (mods & MOD.CTRL)  parts.push("Ctrl");
        if (mods & MOD.SHIFT) parts.push("Shift");
        if (mods & MOD.ALT)   parts.push("Alt");
        if (mods & MOD.WIN)   parts.push("Win");
        if (vk) parts.push(vkName(vk));
        return parts.join("+");
    }

    function isModifierVk(vk) {
        return vk === VK.CTRL || vk === VK.SHIFT || vk === VK.ALT
            || vk === VK.LWIN || vk === VK.RWIN;
    }

    function setVkNames(pairs) {
        if (!pairs || typeof pairs.length !== "number") return;
        var map = {};
        for (var i = 0; i < pairs.length; ++i) {
            var p = pairs[i];
            if (p && p.length >= 2) map[p[0]] = p[1];
        }
        VK_NAMES = map;
    }

    // ───────────────────── Capture instance factory ───────────────────────
    // opts: {
    //   overlayId, previewId, saveBtnId, cancelBtnId   (DOM ids, with or
    //                                                    without leading "#")
    //   onCommit(vk, mods, doubleTap, label)           required
    //   onCancel?()                                    optional
    //   allowDoubleTap?:  bool  (default true)
    //   allowBareModifier?: bool (default false — convert-tool can't fire on
    //                              modifier-alone; HotkeysDialog flips this on)
    // }
    function create(opts) {
        if (!opts || typeof opts.onCommit !== "function") {
            throw new Error("HotkeyCapture.create: onCommit required");
        }
        var overlayId    = stripHash(opts.overlayId    || "capture-overlay");
        var previewId    = stripHash(opts.previewId    || "capture-preview");
        var saveBtnId    = stripHash(opts.saveBtnId    || "btn-capture-save");
        var cancelBtnId  = stripHash(opts.cancelBtnId  || "btn-capture-cancel");
        var allowDoubleTap   = (opts.allowDoubleTap   !== false);
        var allowBareModifier = (opts.allowBareModifier === true);

        var pending = { vk: 0, mods: 0, doubleTap: false, label: "—" };
        var lastTapVk = 0, lastTapTs = 0;
        var isCurrentlyOpen = false;
        var wiredButtons = false;

        function $(id) { return document.getElementById(id); }

        function setOpen(visible) {
            var ov = $(overlayId);
            if (!ov) return;
            ov.style.display = visible ? "block" : "none";
            isCurrentlyOpen = visible;
        }

        function resetPending() {
            pending.vk = 0; pending.mods = 0;
            pending.doubleTap = false; pending.label = "—";
            lastTapVk = 0; lastTapTs = 0;
            var pv = $(previewId); if (pv) pv.textContent = "—";
            var sv = $(saveBtnId); if (sv) sv.setAttribute("disabled", "disabled");
        }

        function open() {
            resetPending();
            wireButtonsOnce();
            setOpen(true);
            // Document-level keydown handler is attached once at module init
            // and reads `isCurrentlyOpen` to decide whether to capture.
        }

        function close() {
            setOpen(false);
            if (opts.onCancel) opts.onCancel();
        }

        function commit() {
            if (!pending.vk && !(allowBareModifier && pending.mods)) return;
            var snapshot = {
                vk: pending.vk, mods: pending.mods,
                doubleTap: pending.doubleTap, label: pending.label
            };
            setOpen(false);
            opts.onCommit(snapshot.vk, snapshot.mods,
                          snapshot.doubleTap, snapshot.label);
        }

        function wireButtonsOnce() {
            if (wiredButtons) return;
            var sv = $(saveBtnId);   if (sv) sv.addEventListener("click", commit);
            var cn = $(cancelBtnId); if (cn) cn.addEventListener("click", close);
            wiredButtons = true;
        }

        // Sinking-phase keydown (^keydown) so Alt's menu-accelerator default
        // handler at the Sciter window level doesn't swallow our events.
        document.on("^keydown", function (evt) {
            if (!isCurrentlyOpen) return;
            var vk = codeToVk(evt.code);
            captureKey(evt, vk);
        });

        function captureKey(evt, vk) {
            if (evt.repeat) { evt.preventDefault(); return; }
            if (!vk) { evt.preventDefault(); return; }

            // Double-tap: same key within window → upgrade to 2×.
            var now = Date.now();
            var isDoubleTap = allowDoubleTap
                && (vk === lastTapVk)
                && (now - lastTapTs <= DOUBLE_TAP_WINDOW_MS);
            lastTapVk = vk;
            lastTapTs = now;

            // When allowBareModifier=false (convert-tool), any vk that is
            // itself a modifier is rejected — HotkeyManager's combo loop
            // skips modifier keypresses, so committing such a binding would
            // produce a dead hotkey. Instead, show "Shift+…" as in-progress
            // feedback while the user holds modifiers waiting for the real
            // chord key. preventDefault so the OS doesn't fire menu accels.
            if (isModifierVk(vk) && !allowBareModifier) {
                var heldMods = 0;
                if (evt.ctrlKey)  heldMods |= MOD.CTRL;
                if (evt.shiftKey) heldMods |= MOD.SHIFT;
                if (evt.altKey)   heldMods |= MOD.ALT;
                if (evt.metaKey)  heldMods |= MOD.WIN;
                var hint = formatLabel(0, heldMods, false);
                var pv0 = $(previewId);
                if (pv0) pv0.textContent = (hint ? hint + "+" : "") + "…";
                // Save stays disabled — `pending.vk` stays 0 until a real
                // non-modifier key arrives.
                evt.preventDefault();
                return;
            }

            // Collect chord modifiers from event flags, excluding the
            // modifier we're currently capturing (so {vk=Shift, mods=Ctrl}
            // means "Ctrl+Shift", not "Shift+Shift"). 2× clears mods.
            var mods = 0;
            if (!isDoubleTap) {
                if (evt.ctrlKey  && vk !== VK.CTRL)                      mods |= MOD.CTRL;
                if (evt.shiftKey && vk !== VK.SHIFT)                     mods |= MOD.SHIFT;
                if (evt.altKey   && vk !== VK.ALT)                       mods |= MOD.ALT;
                if (evt.metaKey  && vk !== VK.LWIN && vk !== VK.RWIN)    mods |= MOD.WIN;
            }

            // Modifier-as-key arrives here only when allowBareModifier=true
            // (HotkeysDialog) — committed as a "modifier alone" trigger.
            // The !allowBareModifier case is handled above.

            pending.vk        = vk;
            pending.mods      = mods;
            pending.doubleTap = isDoubleTap;
            pending.label     = formatLabel(vk, mods, isDoubleTap);

            var pv = $(previewId); if (pv) pv.textContent = pending.label;
            var sv = $(saveBtnId); if (sv) sv.removeAttribute("disabled");

            evt.preventDefault();
            evt.stopPropagation();
        }

        return {
            open:   open,
            close:  close,
            isOpen: function () { return isCurrentlyOpen; }
        };
    }

    function stripHash(s) { return s.charAt(0) === "#" ? s.substr(1) : s; }

    // ───────────────────── Export ─────────────────────────────────────────
    global.NextKeyHotkeyCapture = {
        // Constants (read-only references — callers shouldn't mutate).
        VK:  VK,
        MOD: MOD,
        DOUBLE_TAP_WINDOW_MS: DOUBLE_TAP_WINDOW_MS,
        // Pure helpers.
        codeToVk:     codeToVk,
        vkName:       vkName,
        formatLabel:  formatLabel,
        isModifierVk: isModifierVk,
        setVkNames:   setVkNames,
        // Per-dialog capture instance.
        create:       create,
    };

})(window);
