// VKey - Internationalization Helper
// Vietnamese is the default (hardcoded in HTML). English strings are in strings.js.
// On first call, harvests Vietnamese text from DOM into STRINGS.vi so we can
// switch freely between languages without reloading.

function getLang() {
    return document.documentElement.getAttribute("lang") || "vi";
}

function t(key) {
    var lang = getLang();
    if (lang === "vi") return null;
    var dict = window.STRINGS && window.STRINGS[lang];
    return dict ? (dict[key] || null) : null;
}

// Build STRINGS.vi from current DOM (call once before any EN override)
function buildViDict() {
    if (window.STRINGS.vi) return;
    var vi = {};

    document.querySelectorAll("[data-i18n]").forEach(function (el) {
        vi[el.getAttribute("data-i18n")] = el.textContent;
    });
    document.querySelectorAll("[data-i18n-ph]").forEach(function (el) {
        vi["_ph:" + el.getAttribute("data-i18n-ph")] = el.getAttribute("placeholder") || "";
    });
    document.querySelectorAll("[data-i18n-title]").forEach(function (el) {
        vi["_title:" + el.getAttribute("data-i18n-title")] = el.getAttribute("title") || "";
    });
    document.querySelectorAll("[data-i18n-tooltip]").forEach(function (el) {
        vi["_tooltip:" + el.getAttribute("data-i18n-tooltip")] = el.getAttribute("data-tooltip") || "";
    });

    window.STRINGS.vi = vi;
}

function applyTranslations() {
    buildViDict();

    var lang = getLang();
    var dict = window.STRINGS && window.STRINGS[lang];
    var vi = window.STRINGS.vi;
    if (!dict && lang !== "vi") return;

    document.querySelectorAll("[data-i18n]").forEach(function (el) {
        var key = el.getAttribute("data-i18n");
        var text = (dict && dict[key]) || (vi && vi[key]);
        if (text) el.textContent = text;
    });

    document.querySelectorAll("[data-i18n-ph]").forEach(function (el) {
        var key = el.getAttribute("data-i18n-ph");
        var text = (dict && dict[key]) || (vi && vi["_ph:" + key]);
        if (text) el.setAttribute("placeholder", text);
    });

    document.querySelectorAll("[data-i18n-title]").forEach(function (el) {
        var key = el.getAttribute("data-i18n-title");
        var text = (dict && dict[key]) || (vi && vi["_title:" + key]);
        if (text) el.setAttribute("title", text);
    });

    document.querySelectorAll("[data-i18n-tooltip]").forEach(function (el) {
        var key = el.getAttribute("data-i18n-tooltip");
        var text = (dict && dict[key]) || (vi && vi["_tooltip:" + key]);
        if (text) el.setAttribute("data-tooltip", text);
    });

    var titleEl = document.querySelector("title[data-i18n]");
    if (titleEl) {
        var key = titleEl.getAttribute("data-i18n");
        var text = (dict && dict[key]) || (vi && vi[key]);
        if (text) titleEl.textContent = text;
    }
}
