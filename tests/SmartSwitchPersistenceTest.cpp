// VKey - Smart Switch Persistence + ConfigManager V2 schema Tests
// SPDX-License-Identifier: AGPL-3.0-only
//
// Regression coverage for Bug 2 (V toggles silently dropped on Stop)
// and Bug 3 (machine reset loses runtime state) from the 2026-05-28
// smart-switch redesign. See docs/plans/2026-05-28-smart-switch-persistence-design.md.

#include <gtest/gtest.h>

#include "core/config/ConfigManager.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace NextKey {
namespace {

class SmartSwitchPersistenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = L"test_smart_switch.toml";
        utf8_ = "test_smart_switch.toml";
    }
    void TearDown() override {
        std::filesystem::remove(std::filesystem::path(path_));
    }
    void Write(const std::string& content) {
        std::ofstream file(utf8_);
        file << content;
        file.close();
    }
    [[nodiscard]] std::string Read() {
        std::ifstream f(utf8_);
        std::string s((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
        return s;
    }

    std::wstring path_;
    std::string  utf8_;
};

// ─── Save / Flush round-trip ───────────────────────────────────────────

TEST_F(SmartSwitchPersistenceTest, FlushEmptyMapWritesEmptyTable) {
    std::unordered_map<std::wstring, bool> apps;
    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, apps));
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    EXPECT_TRUE(loaded.empty());
}

TEST_F(SmartSwitchPersistenceTest, FlushSingleEntryRoundTrip) {
    std::unordered_map<std::wstring, bool> apps{
        {L"notepad++.exe", false},  // english
    };
    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, apps));
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[L"notepad++.exe"], false);
}

TEST_F(SmartSwitchPersistenceTest, FlushMixedVAndEPersistsBoth) {
    // Bug 3 regression: pre-fix code only persisted English entries.
    std::unordered_map<std::wstring, bool> apps{
        {L"notepad++.exe", false},  // english
        {L"chrome.exe",    true},   // vietnamese
        {L"idman.exe",     false},
    };
    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, apps));
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    ASSERT_EQ(loaded.size(), 3u);
    EXPECT_EQ(loaded[L"notepad++.exe"], false);
    EXPECT_EQ(loaded[L"chrome.exe"],    true);
    EXPECT_EQ(loaded[L"idman.exe"],     false);
}

TEST_F(SmartSwitchPersistenceTest, FlushDeterministicSortOrder) {
    // Save call N+1 produces byte-identical output to call N — clean diffs.
    std::unordered_map<std::wstring, bool> apps{
        {L"zed.exe",          false},
        {L"alacritty.exe",    true},
        {L"firefox.exe",      false},
    };
    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, apps));
    const std::string first = Read();
    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, apps));
    const std::string second = Read();
    EXPECT_EQ(first, second);
    // Keys appear in alphabetical order.
    const auto posAla = first.find("alacritty.exe");
    const auto posFox = first.find("firefox.exe");
    const auto posZed = first.find("zed.exe");
    ASSERT_NE(posAla, std::string::npos);
    ASSERT_NE(posFox, std::string::npos);
    ASSERT_NE(posZed, std::string::npos);
    EXPECT_LT(posAla, posFox);
    EXPECT_LT(posFox, posZed);
}

TEST_F(SmartSwitchPersistenceTest, FlushOverwritesExistingTable) {
    std::unordered_map<std::wstring, bool> first{
        {L"foo.exe", false},
        {L"bar.exe", false},
    };
    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, first));

    std::unordered_map<std::wstring, bool> second{
        {L"baz.exe", true},
    };
    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, second));

    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[L"baz.exe"], true);
    EXPECT_EQ(loaded.count(L"foo.exe"), 0u);
}

TEST_F(SmartSwitchPersistenceTest, FlushPreservesOtherTomlSections) {
    Write(R"(
[input]
method = "telex"
code_table = 1

[features]
spell_check = true
)");

    std::unordered_map<std::wstring, bool> apps{
        {L"notepad++.exe", false},
    };
    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, apps));

    // Original sections survived.
    const std::string after = Read();
    EXPECT_NE(after.find("telex"), std::string::npos);
    EXPECT_NE(after.find("spell_check"), std::string::npos);
    EXPECT_NE(after.find("notepad++.exe"), std::string::npos);
}

// ─── Load: V2 schema ────────────────────────────────────────────────────

TEST_F(SmartSwitchPersistenceTest, LoadV2Schema) {
    Write(R"(
[smart_switch.apps]
"notepad++.exe" = "english"
"chrome.exe"    = "vietnamese"
)");
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[L"notepad++.exe"], false);
    EXPECT_EQ(loaded[L"chrome.exe"],    true);
}

// ─── Load: legacy migration ────────────────────────────────────────────

TEST_F(SmartSwitchPersistenceTest, LoadLegacyMigratesToEnglishMap) {
    Write(R"(
[smart_switch]
english_mode_apps = [ "notepad++.exe", "idman.exe" ]
)");
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[L"notepad++.exe"], false);
    EXPECT_EQ(loaded[L"idman.exe"],     false);
}

TEST_F(SmartSwitchPersistenceTest, LoadV2WinsOverLegacyWhenBothPresent) {
    Write(R"(
[smart_switch]
english_mode_apps = [ "old_legacy.exe" ]

[smart_switch.apps]
"new_v2.exe" = "vietnamese"
)");
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    // V2 table takes precedence; legacy ignored.
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[L"new_v2.exe"], true);
    EXPECT_EQ(loaded.count(L"old_legacy.exe"), 0u);
}

// ─── Load: normalization + safety ──────────────────────────────────────

TEST_F(SmartSwitchPersistenceTest, LoadLowercasesKeys) {
    // TOML written with mixed case. GetExeNameForHwnd always lowercases,
    // so map keys must also be lowercased at load.
    Write(R"(
[smart_switch.apps]
"NotePad++.exe" = "english"
"Chrome.EXE"    = "vietnamese"
)");
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[L"notepad++.exe"], false);
    EXPECT_EQ(loaded[L"chrome.exe"],    true);
}

TEST_F(SmartSwitchPersistenceTest, LoadUnknownModeStringIsSkipped) {
    Write(R"(
[smart_switch.apps]
"notepad++.exe" = "english"
"weird.exe"     = "florescent"
"chrome.exe"    = "vietnamese"
)");
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[L"notepad++.exe"], false);
    EXPECT_EQ(loaded[L"chrome.exe"],    true);
    EXPECT_EQ(loaded.count(L"weird.exe"), 0u);
}

TEST_F(SmartSwitchPersistenceTest, LoadMalformedFileReturnsEmpty) {
    Write("this is not valid TOML at all >>>>");
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    EXPECT_TRUE(loaded.empty());
}

TEST_F(SmartSwitchPersistenceTest, LoadMissingFileReturnsEmpty) {
    // path_ has not been Write()'d → file doesn't exist.
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    EXPECT_TRUE(loaded.empty());
}

// ─── Migration end-to-end: legacy → load → save → V2 ──────────────────

TEST_F(SmartSwitchPersistenceTest, LegacyLoadFollowedBySaveEmitsV2) {
    Write(R"(
[smart_switch]
english_mode_apps = [ "notepad++.exe", "idman.exe" ]
)");
    auto loaded = ConfigManager::LoadSmartSwitchApps(path_);
    ASSERT_EQ(loaded.size(), 2u);

    ASSERT_TRUE(ConfigManager::SaveSmartSwitchApps(path_, loaded));

    const std::string after = Read();
    EXPECT_NE(after.find("[smart_switch.apps]"), std::string::npos);
    EXPECT_NE(after.find("notepad++.exe"), std::string::npos);
    EXPECT_NE(after.find("idman.exe"), std::string::npos);
    // Legacy `english_mode_apps` array section is GONE from the file
    // (clean break — em + anh agreed 2026-05-28).
    EXPECT_EQ(after.find("english_mode_apps"), std::string::npos);
}

}  // namespace
}  // namespace NextKey
