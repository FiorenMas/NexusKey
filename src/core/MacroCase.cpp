// VKey - Macro expansion decision logic implementation
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial

#include "core/MacroCase.h"

#include "core/engine/CodeTableConverter.h"

#include <cwctype>

namespace NextKey::Macro {

namespace {
[[nodiscard]] std::wstring LowerCopy(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}
}  // namespace

MacroPlan Plan(const PlanInputs& in, const CaseMapper& mapper) {
    MacroPlan plan{};

    std::wstring lowerKey = LowerCopy(in.rawMacroBuffer);
    bool matchedExact = false;
    bool matchedViaComposition = false;

    // Priority 1: full buffer (raw exact, fall back to lowered)
    auto it = in.macroTable.find(in.rawMacroBuffer);
    if (it != in.macroTable.end()) {
        matchedExact = true;
    } else {
        it = in.macroTable.find(lowerKey);
    }
    if (it != in.macroTable.end() && in.triggerChar > L' ') {
        plan.isPartOfMacro = true;
    }

    // Priority 2: buffer without trigger char (e.g. "btw" from "btw.")
    if (it == in.macroTable.end() && in.triggerChar > L' ' &&
        lowerKey.size() > 1 && lowerKey.back() == in.triggerChar) {
        std::wstring rawWithoutTrigger =
            in.rawMacroBuffer.substr(0, in.rawMacroBuffer.size() - 1);
        it = in.macroTable.find(rawWithoutTrigger);
        if (it != in.macroTable.end()) {
            matchedExact = true;
        } else {
            it = in.macroTable.find(lowerKey.substr(0, lowerKey.size() - 1));
        }
    }

    // Priority 3/4: composition-based matches stay case-insensitive only.
    if (it == in.macroTable.end() && !in.previousComposition.empty()) {
        std::wstring compKey = LowerCopy(in.previousComposition);
        if (in.triggerChar > L' ') {
            it = in.macroTable.find(compKey + in.triggerChar);
            if (it != in.macroTable.end()) {
                plan.isPartOfMacro = true;
                matchedViaComposition = true;
            }
        }
        if (it == in.macroTable.end()) {
            it = in.macroTable.find(compKey);
            if (it != in.macroTable.end()) matchedViaComposition = true;
        }
    }
    if (it == in.macroTable.end()) return plan;   // matched: false
    plan.matched = true;

    // Backspace count = on-screen characters that need erasing.
    if (matchedViaComposition) {
        if (in.currentCodeTable != CodeTable::Unicode) {
            plan.bsCount = 0;
            for (auto w : in.previousEncodedWidths) plan.bsCount += w;
        } else {
            plan.bsCount = in.previousComposition.size();
        }
    } else if (in.macroCrossCommit) {
        plan.bsCount = in.rawMacroBuffer.size();
        if (in.triggerChar > L' ' && plan.bsCount > 0) --plan.bsCount;
    } else if (!in.previousComposition.empty()) {
        if (in.currentCodeTable != CodeTable::Unicode) {
            plan.bsCount = 0;
            for (auto w : in.previousEncodedWidths) plan.bsCount += w;
        } else {
            plan.bsCount = in.previousComposition.size();
        }
    } else {
        plan.bsCount = in.rawMacroBuffer.size();
        if (in.triggerChar > L' ' && plan.bsCount > 0) --plan.bsCount;
    }

    // Auto-capitalize expansion to match typed case.
    plan.expansion = it->second;
    if (in.autoCapsEnabled && !matchedExact && !matchedViaComposition &&
        !in.rawMacroBuffer.empty() && !plan.expansion.empty()) {
        std::wstring expansionLower = plan.expansion;
        mapper.Lower(expansionLower.data(), expansionLower.size());
        const bool expansionAllLower = (expansionLower == plan.expansion);
        if (expansionAllLower) {
            bool allUpper = true;
            bool anyAlpha = false;
            for (auto c : in.rawMacroBuffer) {
                if (!std::iswalpha(c)) continue;
                anyAlpha = true;
                if (!std::iswupper(c)) { allUpper = false; break; }
            }
            allUpper = allUpper && anyAlpha && in.rawMacroBuffer.size() > 1;
            const bool firstUpper = std::iswupper(in.rawMacroBuffer[0]) != 0;
            if (allUpper) {
                // Skip \n escape: uppercasing 'n' breaks newline detection downstream.
                for (std::size_t i = 0; i < plan.expansion.size(); ++i) {
                    if (plan.expansion[i] == L'\\' && i + 1 < plan.expansion.size() &&
                        plan.expansion[i + 1] == L'n') {
                        ++i;
                    } else {
                        mapper.Upper(&plan.expansion[i], 1);
                    }
                }
            } else if (firstUpper) {
                bool newWord = true;
                for (std::size_t i = 0; i < plan.expansion.size(); ++i) {
                    if (plan.expansion[i] == L'\\' && i + 1 < plan.expansion.size() &&
                        plan.expansion[i + 1] == L'n') {
                        ++i;
                        newWord = true;
                    } else if (std::iswspace(plan.expansion[i])) {
                        newWord = true;
                    } else {
                        if (newWord) {
                            mapper.Upper(&plan.expansion[i], 1);
                            newWord = false;
                        }
                    }
                }
            }
        }
    }

    // Clipboard threshold: Unicode + size > threshold.
    plan.useClipboard = (in.currentCodeTable == CodeTable::Unicode &&
                         plan.expansion.size() > in.clipboardThreshold);

    return plan;
}

std::wstring ExpandEscapesForClipboard(std::wstring_view expansion) {
    std::wstring out;
    out.reserve(expansion.size());
    for (std::size_t i = 0; i < expansion.size(); ++i) {
        if (expansion[i] == L'\\' && i + 1 < expansion.size() && expansion[i + 1] == L'n') {
            out += L"\r\n";
            ++i;
        } else {
            out += expansion[i];
        }
    }
    return out;
}

std::vector<Segment> BuildSegments(std::wstring_view expansion, CodeTable codeTable) {
    std::vector<Segment> segments;
    Segment cur{};
    cur.text.reserve(expansion.size());
    auto flush = [&]() {
        if (!cur.text.empty()) { segments.push_back(std::move(cur)); cur = {}; }
    };
    for (std::size_t i = 0; i < expansion.size(); ++i) {
        if (expansion[i] == L'\\' && i + 1 < expansion.size() && expansion[i + 1] == L'n') {
            flush();
            segments.push_back(Segment{.isReturn = true});
            ++i;
            continue;
        }
        if (codeTable != CodeTable::Unicode) {
            auto enc = CodeTableConverter::ConvertChar(expansion[i], codeTable);
            cur.text += enc.units[0];
            if (enc.count == 2) cur.text += enc.units[1];
        } else {
            cur.text += expansion[i];
        }
    }
    flush();
    return segments;
}

}  // namespace NextKey::Macro
