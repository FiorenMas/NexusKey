// VKey - Logger tests
// SPDX-License-Identifier: AGPL-3.0-only
//
// Test scope:
//  - Runtime gate: Log() is noop when disabled, writes when enabled.
//  - Path override: SetLogPathForTesting routes writes to a known location.
//  - Disabled-by-default: fresh process has no log file until SetEnabled(true)
//    is followed by at least one Log() call (lazy-open).
//  - Idempotent SetEnabled(true)/SetEnabled(false) sequences don't leak state.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "core/Logger.h"

namespace {

#ifdef _WIN32
// Build a unique scratch path under %TEMP%.
std::wstring MakeScratchPath(const wchar_t* tag) {
    wchar_t tempDir[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t buf[MAX_PATH] = {0};
    _snwprintf_s(buf, MAX_PATH, _TRUNCATE,
                 L"%lsvkey_logger_%ls_%lu.log",
                 tempDir, tag, GetCurrentProcessId());
    return std::wstring(buf);
}
#else
std::wstring MakeScratchPath(const wchar_t* tag) {
    char buf[512] = {0};
    std::string tagNarrow;
    for (const wchar_t* p = tag; *p; ++p) tagNarrow.push_back(static_cast<char>(*p));
    std::snprintf(buf, sizeof(buf), "/tmp/vkey_logger_%s_%d.log",
                  tagNarrow.c_str(), static_cast<int>(getpid()));
    std::wstring out;
    for (char* p = buf; *p; ++p) out.push_back(static_cast<wchar_t>(*p));
    return out;
}
#endif

std::string ReadAllNarrow(const std::wstring& wpath) {
#ifdef _WIN32
    std::ifstream in(wpath, std::ios::binary);
#else
    std::string narrow;
    for (wchar_t c : wpath) narrow.push_back(static_cast<char>(c));
    std::ifstream in(narrow, std::ios::binary);
#endif
    if (!in) return std::string();
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}

void RemoveFile(const std::wstring& wpath) {
#ifdef _WIN32
    DeleteFileW(wpath.c_str());
#else
    std::string narrow;
    for (wchar_t c : wpath) narrow.push_back(static_cast<char>(c));
    std::remove(narrow.c_str());
#endif
}

class LoggerTest : public ::testing::Test {
protected:
    std::wstring scratch_;

    void SetUp() override {
        scratch_ = MakeScratchPath(L"unit");
        RemoveFile(scratch_);
        NextKey::Logger::SetEnabled(false);
        NextKey::Logger::SetRoleTag(L"");
        NextKey::Logger::SetLogPathForTesting(scratch_);
    }

    void TearDown() override {
        NextKey::Logger::SetEnabled(false);
        NextKey::Logger::SetLogPathForTesting(L"");
        NextKey::Logger::SetRoleTag(L"");
        RemoveFile(scratch_);
    }
};

TEST_F(LoggerTest, DisabledByDefault) {
    EXPECT_FALSE(NextKey::Logger::IsEnabled());
}

TEST_F(LoggerTest, DisabledLogWritesNothing) {
    NextKey::Logger::Log(L"this should never appear");
    NextKey::Logger::Shutdown();
    EXPECT_TRUE(ReadAllNarrow(scratch_).empty())
        << "Log file should not exist or be empty when logger is disabled";
}

TEST_F(LoggerTest, EnabledLogWritesContent) {
    NextKey::Logger::SetEnabled(true);
    NextKey::Logger::Log(L"unique_marker_ABC123");
    NextKey::Logger::Shutdown();

    std::string contents = ReadAllNarrow(scratch_);
    EXPECT_FALSE(contents.empty()) << "Log file should have content after enabled Log()";
    // UTF-16-with-BOM (Win32 ccs=UTF-8 → file is UTF-8 with BOM) — search for ASCII bytes
    EXPECT_NE(contents.find("unique_marker_ABC123"), std::string::npos)
        << "Log file should contain the marker string";
}

TEST_F(LoggerTest, ToggleOffStopsWrites) {
    NextKey::Logger::SetEnabled(true);
    NextKey::Logger::Log(L"first_line_PRESENT");
    NextKey::Logger::SetEnabled(false);
    NextKey::Logger::Log(L"second_line_MISSING");
    NextKey::Logger::Shutdown();

    std::string contents = ReadAllNarrow(scratch_);
    EXPECT_NE(contents.find("first_line_PRESENT"), std::string::npos);
    EXPECT_EQ(contents.find("second_line_MISSING"), std::string::npos)
        << "Lines written after disable must not appear";
}

TEST_F(LoggerTest, IdempotentEnable) {
    NextKey::Logger::SetEnabled(true);
    NextKey::Logger::SetEnabled(true);
    NextKey::Logger::SetEnabled(true);
    NextKey::Logger::Log(L"once_only_X");
    NextKey::Logger::Shutdown();

    std::string contents = ReadAllNarrow(scratch_);
    // Count occurrences — exactly one
    size_t pos = 0, count = 0;
    while ((pos = contents.find("once_only_X", pos)) != std::string::npos) {
        ++count; ++pos;
    }
    EXPECT_EQ(count, 1u);
}

// ────────────────────────────────────────────────────────────────────
// Role-tag-driven filename format
// Exercises the real ResolvePathUnlocked() (no SetLogPathForTesting override)
// so the tests prove the production filename pattern, not the override path.
// ────────────────────────────────────────────────────────────────────

// Tests below exercise ResolvePathUnlocked() via GetCurrentLogPath() WITHOUT
// triggering Log() so no real file is created — the unit under test is the
// filename shaper, not the file sink. This also avoids "test pollution" if
// the suite re-runs within the same minute as a prior run.
class LoggerRoleTagTest : public ::testing::Test {
protected:
    void SetUp() override {
        NextKey::Logger::SetEnabled(false);
        NextKey::Logger::SetLogPathForTesting(L"");  // bypass override
        NextKey::Logger::SetRoleTag(L"");            // start clean
    }

    void TearDown() override {
        NextKey::Logger::SetEnabled(false);
        NextKey::Logger::SetRoleTag(L"");
    }

    static std::wstring Basename(const std::wstring& p) {
        size_t s = p.find_last_of(L"\\/");
        return (s == std::wstring::npos) ? p : p.substr(s + 1);
    }
};

TEST_F(LoggerRoleTagTest, SetRoleTagShapesFilename) {
    NextKey::Logger::SetRoleTag(L"TestRoleX");

    std::wstring path = NextKey::Logger::GetCurrentLogPath();
    std::wstring base = Basename(path);
    // Expected: VKey_TestRoleX_DDMMYYYY_HHMM.log  (no PID — non-TSF role,
    // no same-minute collision in a fresh test process).
    ASSERT_GE(base.size(), 19u) << "basename too short: width before timestamp";
    EXPECT_EQ(base.substr(0, 15), L"VKey_TestRoleX_")
        << "Role tag missing from filename";
    EXPECT_EQ(base.substr(base.size() - 4), L".log");

    // Strip prefix/suffix, expect "DDMMYYYY_HHMM" — 13 chars, only digits and one underscore.
    std::wstring stamp = base.substr(15, base.size() - 15 - 4);
    EXPECT_EQ(stamp.size(), 13u) << "Expected DDMMYYYY_HHMM (13 chars), got: "
        << std::string(stamp.begin(), stamp.end());
    EXPECT_EQ(stamp[8], L'_');
    for (size_t i = 0; i < stamp.size(); ++i) {
        if (i == 8) continue;
        EXPECT_TRUE(stamp[i] >= L'0' && stamp[i] <= L'9')
            << "Non-digit in timestamp at " << i;
    }

    // No PID suffix for non-TSF roles when there's no collision.
    EXPECT_EQ(base.find(L"_p"), std::wstring::npos)
        << "Non-TSF role should not carry _p<PID> suffix";
}

TEST_F(LoggerRoleTagTest, TsfRoleAlwaysAppendsPidSuffix) {
    NextKey::Logger::SetRoleTag(L"TSF-chrome");

    std::wstring path = NextKey::Logger::GetCurrentLogPath();
    std::wstring base = Basename(path);
    EXPECT_EQ(base.substr(0, 16), L"VKey_TSF-chrome_") << base.c_str();
    EXPECT_NE(base.find(L"_p"), std::wstring::npos)
        << "TSF-* role must include _p<PID> suffix; got: "
        << std::string(base.begin(), base.end());
    EXPECT_EQ(base.substr(base.size() - 4), L".log");
}

TEST_F(LoggerRoleTagTest, FallbackFormatWhenNoRoleTag) {
    // No SetRoleTag — must fall back to the pre-change format so existing
    // callers (and tests that don't opt in) keep their filename shape.
    std::wstring path = NextKey::Logger::GetCurrentLogPath();
    std::wstring base = Basename(path);
    // Legacy format must end with .log and must NOT carry the new _p<PID>
    // suffix that the role-tag path appends. The exact filename differs by
    // platform (POSIX: "vkey.log"; Win32: "VKey_<ProcessTag>_<PID>.log") —
    // the assertions focus on what the change has *not* broken.
    EXPECT_EQ(base.substr(base.size() - 4), L".log");
    EXPECT_EQ(base.find(L"_p"), std::wstring::npos)
        << "Legacy format must not include _p<PID>";
#ifdef _WIN32
    EXPECT_EQ(base.substr(0, 5), L"VKey_") << "Win32 legacy keeps VKey_ prefix";
#else
    EXPECT_EQ(base, L"vkey.log") << "POSIX legacy is fixed name";
#endif
}

TEST_F(LoggerTest, ConcurrentLogDoesNotCrash) {
    // 4 threads × 50 lines each — no torn lines, no crash. Don't test content
    // ordering (atomic append guarantees no interleave within a line, but
    // thread interleave is allowed).
    NextKey::Logger::SetEnabled(true);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < 50; ++i) {
                NextKey::Logger::Log(L"thread_%d_line_%d", t, i);
            }
        });
    }
    for (auto& th : threads) th.join();
    NextKey::Logger::Shutdown();

    std::string contents = ReadAllNarrow(scratch_);
    // Expect 200 lines total — count line breaks.
    size_t lines = 0;
    for (char c : contents) if (c == '\n') ++lines;
    EXPECT_EQ(lines, 200u) << "Each Log() call must produce exactly one line";
}

}  // namespace
