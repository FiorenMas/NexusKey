// tests/core/config/ConfigSnapshotBuilderTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// TDD tests for ConfigSnapshotBuilder::BuildFromToml().
// Phase 1 of docs/plans/2026-05-22-hookengine-degod-probe.md.
//
// Windows-only: BuildFromToml depends on ConfigManager which uses
// Win32 string conversions (WinStrings.h). Tests are compiled only on WIN32
// (CMakeLists.txt puts this file in the if(WIN32) block).

#ifdef _WIN32

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "core/config/ConfigSnapshotBuilder.h"
#include "core/config/ConfigSnapshot.h"

namespace fs = std::filesystem;

class ConfigSnapshotBuilderTest : public ::testing::Test {
protected:
    fs::path tmpToml_;

    void SetUp() override {
        tmpToml_ = fs::temp_directory_path() /
                   ("vkey_snap_test_" +
                    std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                    ".toml");
    }
    void TearDown() override { std::error_code ec; fs::remove(tmpToml_, ec); }

    void WriteToml(const std::string& body) {
        std::ofstream(tmpToml_) << body;
    }
};

TEST_F(ConfigSnapshotBuilderTest, EmptyConfigYieldsEmptySnapshot) {
    WriteToml("");
    auto snap = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, /*excludeApps*/false, /*tsfApps*/false, /*macroEnabled*/false, /*gen*/1);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->generation, 1u);
    EXPECT_TRUE(snap->excludedAppSet.empty());
    EXPECT_TRUE(snap->tsfAppSet.empty());
    EXPECT_TRUE(snap->macroTable.empty());
}

TEST_F(ConfigSnapshotBuilderTest, ExcludedAppsLoadedOnlyWhenEnabled) {
    // ConfigManager reads [excluded_apps].list (not .apps)
    WriteToml(R"(
[excluded_apps]
list = ["chrome.exe", "code.exe"]
)");
    auto snapOff = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, /*excludeApps*/false, false, false, 0);
    EXPECT_TRUE(snapOff->excludedAppSet.empty()) << "feature off -> set must be empty";

    auto snapOn = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, /*excludeApps*/true, false, false, 0);
    EXPECT_EQ(snapOn->excludedAppSet.size(), 2u);
    EXPECT_GT(snapOn->excludedAppSet.count(L"chrome.exe"), 0u);
}

TEST_F(ConfigSnapshotBuilderTest, MacroLoadedOnlyWhenEnabled) {
    WriteToml(R"(
[macros]
"vd" = "vi du"
"vk" = "VKey"
)");
    auto snapOff = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, false, false, /*macroEnabled*/false, 0);
    EXPECT_TRUE(snapOff->macroTable.empty());

    auto snapOn = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, false, false, /*macroEnabled*/true, 0);
    EXPECT_EQ(snapOn->macroTable.size(), 2u);
}

TEST_F(ConfigSnapshotBuilderTest, GenerationPropagated) {
    WriteToml("");
    auto snap = NextKey::ConfigSnapshotBuilder::BuildFromToml(
        tmpToml_, false, false, false, /*gen*/42);
    EXPECT_EQ(snap->generation, 42u);
}

#endif  // _WIN32
