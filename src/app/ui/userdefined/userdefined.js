// User Defined Input Dialog JavaScript
//
// UX model: action-first. Each TypingAction has at most one assigned key.
// Picking an action populates the key field with its current key (if any);
// "Áp dụng" reassigns the action to the typed key, automatically clearing
// the previous mapping. Mirrors Unikey's flow.

// Reverse map maintained in sync with the C++ keymap (action name → key char).
var actionToKey = {};

document.ready = function () {
    initSubDialog(".keymap-list");
    initUserDefinedDialog();
};

function initUserDefinedDialog() {
    var btnApply = document.getElementById("btn-apply");
    var btnClear = document.getElementById("btn-clear");
    var btnImport = document.getElementById("btn-import");
    var btnExport = document.getElementById("btn-export");
    var btnClose = document.getElementById("btn-close");
    var btnLoadTelex = document.getElementById("btn-load-telex");
    var btnLoadVni = document.getElementById("btn-load-vni");
    var keyName = document.getElementById("key-name");
    var keyAction = document.getElementById("key-action");

    if (btnApply) btnApply.addEventListener("click", function () { onApply(); });
    if (btnClear) btnClear.addEventListener("click", function () { onClear(); });
    if (btnImport) btnImport.addEventListener("click", function () { triggerAction("import"); });
    if (btnExport) btnExport.addEventListener("click", function () { triggerAction("export"); });
    if (btnClose) btnClose.addEventListener("click", function () { triggerAction("close"); });
    if (btnLoadTelex) btnLoadTelex.addEventListener("click", function () { triggerAction("load_telex"); });
    if (btnLoadVni) btnLoadVni.addEventListener("click", function () { triggerAction("load_vni"); });

    if (keyAction) {
        // Switching action populates the key field with its current assignment.
        // Initial populate is handled by syncSelectedActionKey() called from C++
        // at the end of populateList() — that runs after the keymap is loaded.
        keyAction.addEventListener("change", function () {
            keyName.value = actionToKey[this.value] || "";
        });
    }

    if (keyName) {
        keyName.addEventListener("input", function () {
            if (this.value.length > 1) {
                this.value = this.value.substring(0, 1);
            }
        });
    }
}

function onApply() {
    var keyField = document.getElementById("key-name");
    var actionField = document.getElementById("key-action");
    if (!keyField || !actionField) return;

    var key = keyField.value;
    var action = actionField.value;
    if (key.length === 0 || !action) return;

    document.getElementById("val-key").value = key;
    document.getElementById("val-key-action").value = action;
    triggerAction("apply");
}

function onClear() {
    var actionField = document.getElementById("key-action");
    if (!actionField) return;
    var action = actionField.value;
    if (!action) return;

    document.getElementById("val-key-action").value = action;
    triggerAction("clear_action");
}

function selectKeyItem(element, key, action) {
    var items = document.querySelectorAll(".keymap-item");
    for (var i = 0; i < items.length; i++) {
        items[i].classList.remove("selected");
    }
    element.classList.add("selected");

    document.getElementById("key-action").value = action;
    document.getElementById("key-name").value = key;
}

function triggerAction(action) {
    var actionInput = document.getElementById("val-action");
    if (actionInput) {
        actionInput.value = action;
        var event = new Event("change", { bubbles: true });
        actionInput.dispatchEvent(event);
    }
}

// Resolve a TypingAction enum name to its localized label.
// Source-of-truth is the dropdown's <option> text — Vietnamese is the default
// language hardcoded in HTML and t() only resolves English. Falls back to
// strings.js then to the raw action name.
function getActionLabel(action) {
    var opt = document.querySelector('#key-action option[value="' + action + '"]');
    if (opt) {
        var text = (opt.textContent || "").trim();
        if (text) return text;
    }
    var fromDict = (typeof t === "function" ? t("ud.act." + action) : "");
    return fromDict || action;
}

// Called by C++ on every populateList(). Label is resolved JS-side so C++
// doesn't need i18n knowledge.
function addKeyToMap(key, action) {
    actionToKey[action] = key;

    var list = document.getElementById("keymap-list");
    if (!list) return;

    var actionLabel = getActionLabel(action);

    var item = document.createElement("div");
    item.className = "keymap-item";
    item.setAttribute("data-key", key);
    item.setAttribute("data-action", action);

    var displayKey = key === " " ? "(Space)" : key;

    item.innerHTML = '<span class="keymap-item-key">' + escapeHtml(displayKey) + '</span>' +
        '<span class="keymap-item-action">' + escapeHtml(actionLabel) + '</span>';

    item.addEventListener("click", function () {
        selectKeyItem(this, key, action);
    });

    list.appendChild(item);
}

function clearKeyMap() {
    actionToKey = {};
    var list = document.getElementById("keymap-list");
    if (list) list.innerHTML = "";
}

// Called by C++ at the end of populateList() to refresh the key field for
// the currently-selected action. Solves the race where document.ready runs
// before the C++ ctor finishes populating the keymap.
function syncSelectedActionKey() {
    var keyAction = document.getElementById("key-action");
    var keyName = document.getElementById("key-name");
    if (keyAction && keyName) {
        keyName.value = actionToKey[keyAction.value] || "";
    }
}

function escapeHtml(text) {
    var div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
}
