// VKey - UpdateSecurity Unit Tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include "app/system/UpdateSecurity.h"

using namespace NextKey;

// ── SEC-002: PowerShell escaping ───────────────────────────────────────────

TEST(EscapePowerShellSingleQuoteTest, NoQuotes) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"C:\\Users\\Admin\\file.zip"),
              L"C:\\Users\\Admin\\file.zip");
}

TEST(EscapePowerShellSingleQuoteTest, SingleQuoteInUsername) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"C:\\Users\\O'Brien\\AppData\\file.zip"),
              L"C:\\Users\\O''Brien\\AppData\\file.zip");
}

TEST(EscapePowerShellSingleQuoteTest, MultipleQuotes) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"it's a 'test'"),
              L"it''s a ''test''");
}

TEST(EscapePowerShellSingleQuoteTest, EmptyString) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L""), L"");
}

TEST(EscapePowerShellSingleQuoteTest, OnlyQuotes) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"'''"), L"''''''");
}

TEST(EscapePowerShellSingleQuoteTest, ConsecutiveQuotes) {
    EXPECT_EQ(EscapePowerShellSingleQuote(L"a''b"), L"a''''b");
}

// ── SEC-003: URL domain validation (wide) ──────────────────────────────────

TEST(IsAllowedDownloadUrlWideTest, GitHubReleasesUrl) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        L"https://github.com/phatMT97/VKey/releases/download/v2.0.0/VKey-x64.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, GitHubObjectsUrl) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        L"https://objects.githubusercontent.com/github-production-release-asset/12345/abc.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, GitHubCodeloadUrl) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        L"https://codeload.github.com/phatMT97/VKey/zip/refs/tags/v2.0.0"));
}

TEST(IsAllowedDownloadUrlWideTest, RejectsArbitraryDomain) {
    EXPECT_FALSE(IsAllowedDownloadUrl(L"https://evil.com/VKey-x64.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, RejectsHttp) {
    EXPECT_FALSE(IsAllowedDownloadUrl(L"http://github.com/foo/bar.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, RejectsSimilarDomain) {
    EXPECT_FALSE(IsAllowedDownloadUrl(L"https://github.com.evil.com/foo.zip"));
}

TEST(IsAllowedDownloadUrlWideTest, RejectsEmpty) {
    EXPECT_FALSE(IsAllowedDownloadUrl(std::wstring{}));
}

TEST(IsAllowedDownloadUrlWideTest, CaseInsensitive) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        L"HTTPS://GITHUB.COM/phatMT97/VKey/releases/download/v2.0.0/VKey-x64.zip"));
}

// ── SEC-003: URL domain validation (narrow) ────────────────────────────────

TEST(IsAllowedDownloadUrlNarrowTest, GitHubReleasesUrl) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        std::string("https://github.com/phatMT97/VKey/releases/download/v2.0.0/VKey-x64.zip")));
}

TEST(IsAllowedDownloadUrlNarrowTest, RejectsArbitraryDomain) {
    EXPECT_FALSE(IsAllowedDownloadUrl(std::string("https://evil.com/VKey-x64.zip")));
}

TEST(IsAllowedDownloadUrlNarrowTest, RejectsSubdomain) {
    EXPECT_FALSE(IsAllowedDownloadUrl(std::string("https://github.com.evil.com/foo.zip")));
}

TEST(IsAllowedDownloadUrlNarrowTest, CaseInsensitive) {
    EXPECT_TRUE(IsAllowedDownloadUrl(
        std::string("HTTPS://GITHUB.COM/phatMT97/VKey/releases/download/v2.0.0/VKey-x64.zip")));
}

// ── SEC-001: ParseSha256File (cross-platform string parsing) ─────────────

TEST(ParseSha256FileTest, StandardFormat) {
    // GNU coreutils sha256sum: "<hash>  <filename>"
    std::string content = "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890  NextKey-x64.zip\n";
    EXPECT_EQ(ParseSha256File(content),
              "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
}

TEST(ParseSha256FileTest, HashOnly) {
    std::string content = "ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890\n";
    EXPECT_EQ(ParseSha256File(content),
              "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
}

TEST(ParseSha256FileTest, EmptyContent) {
    EXPECT_EQ(ParseSha256File(""), "");
}

TEST(ParseSha256FileTest, TooShortHash) {
    EXPECT_EQ(ParseSha256File("abcdef123"), "");
}

TEST(ParseSha256FileTest, TooLongHash) {
    EXPECT_EQ(ParseSha256File("abcdef1234567890abcdef1234567890abcdef1234567890abcdef12345678901"), "");
}

TEST(ParseSha256FileTest, InvalidHexChar) {
    EXPECT_EQ(ParseSha256File("abcdef1234567890abcdef1234567890abcdef1234567890abcdefXX34567890"), "");
}

TEST(ParseSha256FileTest, WindowsLineEnding) {
    std::string content = "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890  NextKey-x64.zip\r\n";
    EXPECT_EQ(ParseSha256File(content),
              "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
}

TEST(ParseSha256FileTest, UppercaseNormalizedToLower) {
    std::string content = "ABCDEF0000000000ABCDEF0000000000ABCDEF0000000000ABCDEF0000000000";
    EXPECT_EQ(ParseSha256File(content),
              "abcdef0000000000abcdef0000000000abcdef0000000000abcdef0000000000");
}
