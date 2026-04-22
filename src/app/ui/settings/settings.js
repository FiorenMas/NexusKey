// NexusKey Settings JavaScript
// Handles div-based toggles with hidden inputs for VALUE_CHANGED events

document.on("ready", function () {
    // Enable blur-behind effect (Sciter built-in)
    // "dark" = dark tint, "source-auto" = automatic blur source
    requestAnimationFrame(function() {
        if (!document.body.classList.contains("win10")) {
            Window.this.blurBehind = "dark source-auto";
        } else {
            Window.this.blurBehind = "none";
        }
    });

    // Apply i18n translations
    if (typeof applyTranslations === "function") applyTranslations();

    initializeToggles();
    initializeAdvancedPanel();
    initializeOpacitySlider();
    initializeScrollbarResize(".tab-body");
    initializeSwitchKeyDisplay();
    initializeTabPanels();
    initializeDropdownTooltips();
});

// Set tooltip on all dropdowns to show only the selected item text
function initializeDropdownTooltips() {
    document.querySelectorAll("select").forEach(function (select) {
        var selected = select.querySelector("option:checked");
        if (selected) select.setAttribute("title", selected.textContent);
    });
}

// Initialize switch key input - convert space char to "Space" display
function initializeSwitchKeyDisplay() {
    var input = document.getElementById("switch-key-char");
    if (input && input.value === " ") {
        input.value = "Space";
    }
}

// ============================================
// RESTART BANNER - Called from C++
// ============================================
// showUpdateBanner(state) — state: 0=hide, 1=pending copy, 2=mismatch copy.
// Triggered by SettingsDialog::initializeUI() after reading SharedState flags.
function showUpdateBanner(state) {
    var banner = document.getElementById("update-banner");
    if (!banner) return;

    if (state === 0) {
        banner.style.display = "none";
        return;
    }

    var msgKey = (state === 1) ? "update.banner.pending" : "update.banner.mismatch";
    var text = document.getElementById("update-banner-text");
    var btnRestart = document.getElementById("update-banner-restart");
    var btnLater = document.getElementById("update-banner-later");

    if (text) text.textContent = (typeof t === "function") ? t(msgKey)
                                                            : "Restart Windows to finish update.";
    if (btnRestart) btnRestart.textContent = (typeof t === "function")
        ? t("update.banner.restartNow") : "Restart now";
    if (btnLater) btnLater.textContent = (typeof t === "function")
        ? t("update.banner.later") : "Later";

    banner.style.display = "";

    if (btnRestart) btnRestart.onclick = function () {
        Window.this.requestRestartWindows();
    };
    if (btnLater) btnLater.onclick = function () {
        banner.style.display = "none";
        // Do not persist — banner re-appears on next dialog open (design §4).
    };
}

// ============================================
// THEME MANAGEMENT - Called from C++
// ============================================
// setTheme(isDark) - Called from C++ to apply dark/light theme
// This avoids using eval() for security and clean code
function setTheme(isDark) {
    if (isDark) {
        document.body.classList.add("dark");
    } else {
        document.body.classList.remove("dark");
    }

    // Update background color based on theme and current opacity
    updateBackgroundForTheme(isDark);

    // Fix update-card toggle thumb color in dark mode
    fixUpdateToggleThumb();
}

// Force update-card toggle thumb to be white (workaround for Sciter CSS quirk)
function fixUpdateToggleThumb() {
    var thumb = document.getElementById("update-toggle-thumb");
    if (thumb) {
        thumb.style.backgroundColor = "#FFFFFF";
        thumb.style.background = "#FFFFFF";
    }
}

// Helper to update background rgba based on theme
function updateBackgroundForTheme(isDark) {
    var container = document.getElementById("main-container");
    var hiddenInput = document.getElementById("val-bg-opacity");
    if (container && hiddenInput) {
        var opacity = parseInt(hiddenInput.value || "80") / 100;
        if (isDark) {
            // Semi-transparent blue-gray for frosted glass
            container.style.backgroundColor = "rgba(18, 20, 28, " + opacity + ")";
        } else {
            container.style.backgroundColor = "rgba(255, 255, 255, " + opacity + ")";
        }
    }
}


function initializeToggles() {
    // Get all toggle elements and attach handlers
    const allToggles = document.querySelectorAll(".toggle-switch, .toggle-switch-small");

    allToggles.forEach(function (toggle) {
        toggle.onclick = function (evt) {
            // Block clicks on disabled toggles (child of unchecked parent)
            if (this.classList.contains("disabled")) return false;

            const isChecked = this.classList.contains("checked");

            if (isChecked) {
                this.classList.remove("checked");
            } else {
                this.classList.add("checked");
            }

            const id = this.id;
            const newState = !isChecked;

            // English UI toggle: switch language immediately in JS
            if (id === "english-ui") {
                document.documentElement.setAttribute("lang", newState ? "en" : "vi");
                applyTranslations();
            }

            // Show-advanced toggle: update container class
            if (id === "show-advanced") {
                const container = document.getElementById("main-container");
                if (container) {
                    if (newState) {
                        container.classList.add("expanded");
                        // Force sync layout before C++ measures DOM
                        Window.this.update();
                        // Tab indicator couldn't be positioned during init
                        // (section was display:none → offsetLeft/Width = 0).
                        // Position it now that the section is visible.
                        requestAnimationFrame(function() {
                            var activeTab = document.querySelector(".tab-item.active");
                            if (activeTab) updateTabIndicator(activeTab);
                        });
                    } else {
                        container.classList.remove("expanded");
                        Window.this.update();
                    }
                }
            }


            // Spell check controls child toggles (zwjf, restore-key, exclusions button)
            if (id === "spell-check") {
                updateSpellCheckChildren(newState);
            }

            // Defer C++ notification to next frame so toggle animation starts instantly.
            requestAnimationFrame(function() {
                var hiddenInput = document.getElementById("val-" + id);
                if (hiddenInput) {
                    hiddenInput.value = newState ? "1" : "0";
                    hiddenInput.dispatchEvent(new Event("change", { bubbles: true }));
                }
            });

            return true;
        };
    });

    // Initial state: sync child toggles with spell-check parent
    var spellToggle = document.getElementById("spell-check");
    if (spellToggle) {
        updateSpellCheckChildren(spellToggle.classList.contains("checked"));
    }
}

// Enable/disable spell check child options based on parent state
function updateSpellCheckChildren(spellEnabled) {
    var childIds = ["allow-zwjf", "restore-key"];
    childIds.forEach(function(id) {
        var toggle = document.getElementById(id);
        if (!toggle) return;
        var row = toggle.closest(".setting-row");

        if (spellEnabled) {
            toggle.classList.remove("disabled");
            if (row) row.classList.remove("disabled");
        } else {
            toggle.classList.add("disabled");
            if (row) row.classList.add("disabled");
        }
    });

    // Exclusions button
    var btnExcl = document.getElementById("btn-spell-exclusions");
    if (btnExcl) {
        btnExcl.state.disabled = !spellEnabled;
    }
    var rowExcl = document.getElementById("row-spell-exclusions");
    if (rowExcl) {
        if (spellEnabled) {
            rowExcl.classList.remove("disabled");
        } else {
            rowExcl.classList.add("disabled");
        }
    }
}

// Initialize advanced panel toggle and tabs
function initializeAdvancedPanel() {
    // Tab switching
    const tabItems = document.querySelectorAll(".tab-item");
    tabItems.forEach(function (tab) {
        tab.onclick = function () {
            switchTab(this.getAttribute("data-tab"));
            return true;
        };
    });

    // Initialize indicator position once layout is parsed
    requestAnimationFrame(function() {
        var activeTab = document.querySelector(".tab-item.active");
        if (activeTab) updateTabIndicator(activeTab);
    });
}

// Fix Sciter animation lag caused by hover hit-testing on transparent windows.
// On WS_EX_LAYERED windows, every mouse move triggers hit-test → style recalc →
// full surface repaint.  During animation this doubles the render cost.
// Strategy: suppress BOTH pointer hit-testing AND all hover transitions so that
// mouse movement over children is essentially free (no style changes → no repaints).
var animTimeout = null;
function lockPointerEvents(duration) {
    var container = document.getElementById("main-container");
    if (!container) return;

    container.classList.add("animating");
    container.state.disabled = true;
    if (animTimeout) clearTimeout(animTimeout);

    animTimeout = setTimeout(function() {
        container.state.disabled = false;
        container.classList.remove("animating");
    }, duration);
}

function updateTabIndicator(activeTab) {
    var indicator = document.getElementById("tab-indicator");
    if (!indicator || !activeTab) return;
    
    var left = activeTab.offsetLeft;
    var width = activeTab.offsetWidth;
    
    if (left >= 0 && width > 0) {
        indicator.style.left = left + "px";
        indicator.style.width = width + "px";
    }
}

// Initialize tab panels with Sciter native state (first tab expanded)
function initializeTabPanels() {
    const tabPanels = document.querySelectorAll(".tab-panel");
    tabPanels.forEach(function (panel, index) {
        if (index === 0) {
            panel.state.expanded = true;
            panel.state.collapsed = false;
        } else {
            panel.state.expanded = false;
            panel.state.collapsed = true;
        }
    });

    // Lock pointer events during tab panel slide/fade transition
    lockPointerEvents(250);
}

// Switch between tabs - using Sciter native state pattern
function switchTab(tabIndex) {
    // Update active tab header (visual only, use classList)
    const tabItems = document.querySelectorAll(".tab-item");
    var activeTab = null;
    tabItems.forEach(function (tab) {
        if (tab.getAttribute("data-tab") === tabIndex) {
            tab.classList.add("active");
            activeTab = tab;
        } else {
            tab.classList.remove("active");
        }
    });

    if (activeTab) updateTabIndicator(activeTab);

    // Update tab panels using Sciter native state (no flicker)
    const tabPanels = document.querySelectorAll(".tab-panel");
    tabPanels.forEach(function (panel) {
        const panelIndex = panel.id.replace("tab-panel-", "");
        if (panelIndex === tabIndex) {
            panel.state.expanded = true;
            panel.state.collapsed = false;
        } else {
            panel.state.expanded = false;
            panel.state.collapsed = true;
        }
    });

    // Notify C++ to recalculate window size for new tab content
    const tabChangeInput = document.getElementById("val-tab-change");
    if (tabChangeInput) {
        tabChangeInput.value = tabIndex;
        tabChangeInput.dispatchEvent(new Event("change", { bubbles: true }));
    }
}

// Handle dropdown changes (already works via C++ VALUE_CHANGED handler)
document.on("change", "select", function (evt, select) {
    const id = select.id || select.getAttribute("id");
    const value = parseInt(select.value);

    // Update tooltip to show only the selected item text
    var selected = select.querySelector("option:checked");
    if (selected) select.setAttribute("title", selected.textContent);
});

// Handle text input changes - display "Space" for space character
document.on("change", "#switch-key-char", function (evt, input) {
    let keyChar = input.value;

    if (keyChar === " ") {
        // Space character typed - display "Space"
        input.value = "Space";
    } else if (keyChar === "Space") {
        // Full "Space" text - keep it (already displayed correctly)
    } else if (keyChar.length === 0) {
        // Empty - user deleted everything
    } else if (keyChar.length === 1) {
        // Single character input - uppercase it
        input.value = keyChar.toUpperCase();
    } else {
        // Partial text (like "Spac", "Sp" from deletion) - clear it
        input.value = "";
    }
});

// Real-time conversion while typing space
document.on("input", "#switch-key-char", function (evt, input) {
    if (input.value === " ") {
        input.value = "Space";
    }
});

// Handle icon dropdown change - show/hide custom color row
document.on("change", "#modern-icon", function (evt, select) {
    var colorRow = document.getElementById("custom-color-row");
    if (colorRow) {
        var value = select.value;
        // Show color row only when Custom (value=3) is selected
        colorRow.style.display = (value == "3" || value == 3) ? "flex" : "none";

        // Notify C++ to recalculate window size for the changed row
        // Use setTimeout to let Sciter update the style attribute before recalc
        setTimeout(function () {
            const tabChangeInput = document.getElementById("val-tab-change");
            if (tabChangeInput) {
                tabChangeInput.value = "icon-change";
                tabChangeInput.dispatchEvent(new Event("change", { bubbles: true }));
            }
        }, 50);
    }
});

// Handle button clicks
document.on("click", "button", function (evt, button) {
    const id = button.id || button.getAttribute("id");
    // Color buttons (btn-color-v, btn-color-e, btn-reset-colors) are handled by C++
    // which opens Windows ChooseColor dialog
});

// ============================================
// CUSTOM OPACITY SLIDER - Background Transparency
// ============================================
var sliderDragging = false;

function initializeOpacitySlider() {
    var slider = document.getElementById("bg-opacity-slider");
    var thumb = document.getElementById("bg-opacity-thumb");
    var fill = document.getElementById("bg-opacity-fill");
    var valueLabel = document.getElementById("bg-opacity-value");
    var hiddenInput = document.getElementById("val-bg-opacity");

    if (!slider || !thumb) return;

    // Click on track to set value
    slider.onmousedown = function (evt) {
        sliderDragging = true;
        slider.state.capture(true); // Capture mouse even outside window
        updateSliderFromMouse(evt, slider, thumb, fill, valueLabel, hiddenInput);
    };

    // Drag thumb — use slider (capture target) instead of document
    slider.onmousemove = function (evt) {
        if (sliderDragging) {
            updateSliderFromMouse(evt, slider, thumb, fill, valueLabel, hiddenInput);
        }
    };

    slider.onmouseup = function (evt) {
        if (sliderDragging) {
            sliderDragging = false;
            slider.state.capture(false);
            // Save to C++ on release
            hiddenInput.dispatchEvent(new Event("change", { bubbles: true }));
        }
    };
}

function updateSliderFromMouse(evt, slider, thumb, fill, valueLabel, hiddenInput) {
    var rect = slider.getBoundingClientRect();
    var x = evt.clientX - rect.left;
    var width = rect.width;

    // Clamp to 0-100%
    var percent = Math.max(0, Math.min(100, (x / width) * 100));
    var value = Math.round(percent);

    // Update UI
    thumb.style.left = percent + "%";
    fill.style.width = percent + "%";
    valueLabel.textContent = value + "%";
    hiddenInput.value = value.toString();

    // Apply background immediately (respect dark/light mode)
    var opacity = value / 100;
    var container = document.getElementById("main-container");
    if (container) {
        var isDark = document.body.classList.contains("dark");
        if (isDark) {
            // Semi-transparent blue-gray for frosted glass effect
            container.style.backgroundColor = "rgba(18, 20, 28, " + opacity + ")";
        } else {
            container.style.backgroundColor = "rgba(255, 255, 255, " + opacity + ")";
        }
    }
}

// Called from C++ to set initial opacity value.
// Overrides utils.js setBackgroundOpacity to also update slider UI elements.
function setBackgroundOpacity(value) {
    var thumb = document.getElementById("bg-opacity-thumb");
    var fill = document.getElementById("bg-opacity-fill");
    var valueLabel = document.getElementById("bg-opacity-value");
    var hiddenInput = document.getElementById("val-bg-opacity");

    if (thumb && fill) {
        thumb.style.left = value + "%";
        fill.style.width = value + "%";
        valueLabel.textContent = value + "%";
        hiddenInput.value = value.toString();

        // Apply background directly on container (respect dark/light mode)
        var opacity = value / 100;
        var container = document.getElementById("main-container");
        if (container) {
            var isDark = document.body.classList.contains("dark");
            if (isDark) {
                container.style.backgroundColor = "rgba(18, 20, 28, " + opacity + ")";
            } else {
                container.style.backgroundColor = "rgba(255, 255, 255, " + opacity + ")";
            }
        }
    }
}
