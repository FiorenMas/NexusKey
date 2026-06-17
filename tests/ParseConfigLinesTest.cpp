// VKey — ParseConfigLines helper unit tests (Linux-portable).
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pins the line-filtering contract used by all dialog Import handlers
// (ExcludedApps, TsfApps, SpellExclusions, MacroTable × Sciter+Classic).
// The helper consolidates 6 lines of duplicated parsing logic across 8
// call sites. These tests lock the filter rules so future changes there
// (e.g. add UTF-8 BOM detection, '#' comments) update exactly one place.

#include <gtest/gtest.h>

#include "app/helpers/AppHelpers.h"

#include <sstream>
#include <string>
#include <vector>

namespace NextKey {
namespace {

std::vector<std::string> Collect(const std::string& input) {
    std::istringstream stream(input);
    std::vector<std::string> out;
    ParseConfigLines(stream, [&](const std::string& line) {
        out.push_back(line);
    });
    return out;
}

TEST(ParseConfigLines, EmptyInputYieldsNothing) {
    EXPECT_TRUE(Collect("").empty());
}

TEST(ParseConfigLines, SkipsBlankLines) {
    auto lines = Collect("a\n\nb\n\n\nc\n");
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "a");
    EXPECT_EQ(lines[1], "b");
    EXPECT_EQ(lines[2], "c");
}

TEST(ParseConfigLines, SkipsSemicolonComments) {
    auto lines = Collect(";header line\napp1\n;another comment\napp2\n");
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "app1");
    EXPECT_EQ(lines[1], "app2");
}

TEST(ParseConfigLines, StripsTrailingCR) {
    // Windows-style CRLF — the \r before \n must be stripped before
    // the line is handed to the handler.
    auto lines = Collect("foo\r\nbar\r\n");
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "foo");
    EXPECT_EQ(lines[1], "bar");
}

TEST(ParseConfigLines, LastLineWithoutNewline) {
    auto lines = Collect("only");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "only");
}

TEST(ParseConfigLines, CRLFLineWithSemicolonStillSkipped) {
    // A semicolon-leading line ending in CRLF — both the CR strip AND
    // the comment skip must fire so the handler never sees it.
    auto lines = Collect(";VKey Header\r\nvalue\r\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "value");
}

TEST(ParseConfigLines, KeyValueLinesArePassedThrough) {
    // MacroTable format: caller splits on ':' inside the lambda. Helper
    // doesn't interpret content — just delivers the raw line.
    auto lines = Collect("alpha:beta\ngamma:delta\n");
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "alpha:beta");
    EXPECT_EQ(lines[1], "gamma:delta");
}

TEST(ParseConfigLines, StripsUtf8BomFromFirstLine) {
    // Notepad on Windows saves UTF-8 files with EF BB BF prefix by
    // default. The first content line must arrive without the BOM
    // bytes, otherwise apps see a phantom 3-byte prefix on entry 1.
    const std::string bom = "\xEF\xBB\xBF";
    auto lines = Collect(bom + "entry1\nentry2\n");
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "entry1");
    EXPECT_EQ(lines[1], "entry2");
}

TEST(ParseConfigLines, BomLeavingEmptyLineIsSkipped) {
    // First line is JUST the BOM — after strip it's empty, so skip.
    const std::string bom = "\xEF\xBB\xBF";
    auto lines = Collect(bom + "\nreal\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "real");
}

TEST(ParseConfigLines, BomThenCommentBothFiltered) {
    // BOM + ";header" on first line: BOM strips to ";header", which
    // then falls under the comment skip — handler never sees it.
    const std::string bom = "\xEF\xBB\xBF";
    auto lines = Collect(bom + ";VKey Spell Exclusions\nword1\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "word1");
}

}  // namespace
}  // namespace NextKey
