// VKey - HookContextAnchor Unit Tests
// SPDX-License-Identifier: AGPL-3.0-only

#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "core/ipc/SharedState.h"

namespace NextKey {
namespace {

// Helper: build a uint16_t buffer from a u"..." literal.
static std::vector<uint16_t> U(const char16_t* s) {
    std::vector<uint16_t> v;
    while (*s) v.push_back(static_cast<uint16_t>(*s++));
    return v;
}

// ============================================================================
// DeriveAnchorFromPreceding — scan logic
// ============================================================================

TEST(HookContextAnchorTest, Derive_EmptyBuffer_AllStartFlagsTrue) {
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(nullptr, 0, a);
    EXPECT_EQ(a.isSentenceStart, 1);
    EXPECT_EQ(a.isLineStart, 1);
    EXPECT_EQ(a.isWordStart, 1);
    EXPECT_EQ(a.syllableLen, 0);
}

TEST(HookContextAnchorTest, Derive_AfterPeriodAndSpace_SentenceStart) {
    auto buf = U(u"hello. ");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 1);
    EXPECT_EQ(a.isLineStart, 0);
    EXPECT_EQ(a.isWordStart, 1);
}

TEST(HookContextAnchorTest, Derive_AfterPeriodMultipleWhitespace_SentenceStart) {
    auto buf = U(u"hello.\t\t ");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 1);
    EXPECT_EQ(a.isLineStart, 0);
    EXPECT_EQ(a.isWordStart, 1);
}

TEST(HookContextAnchorTest, Derive_AfterQuestionMark_SentenceStart) {
    auto buf = U(u"ok? ");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 1);
}

TEST(HookContextAnchorTest, Derive_AfterExclamation_SentenceStart) {
    auto buf = U(u"wow! ");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 1);
}

// ".com" / "Hi." / "What?" — punct glued to cursor with no trailing whitespace.
// Domains and file extensions must NOT be treated as sentence starts.
TEST(HookContextAnchorTest, Derive_PeriodGluedToCursor_NotSentenceStart) {
    auto buf = U(u"chrome.");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 0);
    EXPECT_EQ(a.isLineStart, 0);
    EXPECT_EQ(a.isWordStart, 0);
}

TEST(HookContextAnchorTest, Derive_PeriodFollowedByLetter_NotSentenceStart) {
    auto buf = U(u"chrome.com");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 0);
}

TEST(HookContextAnchorTest, Derive_ShortWordWithPeriodGlued_NotSentenceStart) {
    auto buf = U(u"Hi.");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 0);
}

TEST(HookContextAnchorTest, Derive_QuestionAndExclamGlued_NotSentenceStart) {
    auto bufQ = U(u"What?");
    auto bufE = U(u"Wow!");
    HookContextAnchor aQ{}, aE{};
    DeriveAnchorFromPreceding(bufQ.data(), bufQ.size(), aQ);
    DeriveAnchorFromPreceding(bufE.data(), bufE.size(), aE);
    EXPECT_EQ(aQ.isSentenceStart, 0);
    EXPECT_EQ(aE.isSentenceStart, 0);
}

// Multi-space handling: the whitespace skip loop walks across any run of
// spaces/tabs, so sentence-start fires regardless of how many spaces the user
// typed between the punct and the cursor.
TEST(HookContextAnchorTest, Derive_PunctFollowedByMultipleSpaces_SentenceStart) {
    auto buf2 = U(u"end.  ");     // two spaces
    auto buf3 = U(u"hey!\t ");    // tab + space
    HookContextAnchor a2{}, a3{};
    DeriveAnchorFromPreceding(buf2.data(), buf2.size(), a2);
    DeriveAnchorFromPreceding(buf3.data(), buf3.size(), a3);
    EXPECT_EQ(a2.isSentenceStart, 1);
    EXPECT_EQ(a2.isWordStart, 1);
    EXPECT_EQ(a3.isSentenceStart, 1);
    EXPECT_EQ(a3.isWordStart, 1);
}

TEST(HookContextAnchorTest, Derive_AfterNewline_LineStartNotSentence) {
    auto buf = U(u"hello\n");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 0);
    EXPECT_EQ(a.isLineStart, 1);
    EXPECT_EQ(a.isWordStart, 1);
}

TEST(HookContextAnchorTest, Derive_AfterCommaNewline_LineStartOnly) {
    auto buf = U(u"hello,\n");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 0);
    EXPECT_EQ(a.isLineStart, 1);
}

TEST(HookContextAnchorTest, Derive_MidWord_NoStartFlags) {
    auto buf = U(u"hello");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 0);
    EXPECT_EQ(a.isLineStart, 0);
    EXPECT_EQ(a.isWordStart, 0);
    // currentSyllable = "hello"
    ASSERT_EQ(a.syllableLen, 5);
    EXPECT_EQ(a.currentSyllable[0], u'h');
    EXPECT_EQ(a.currentSyllable[4], u'o');
}

TEST(HookContextAnchorTest, Derive_AfterWordSpace_WordStartNotSyllable) {
    auto buf = U(u"hello ");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 0);
    EXPECT_EQ(a.isLineStart, 0);
    EXPECT_EQ(a.isWordStart, 1);
    EXPECT_EQ(a.syllableLen, 0);
}

TEST(HookContextAnchorTest, Derive_Syllable_TruncatesAt16Chars) {
    // 20 non-whitespace chars — should keep the LAST 16.
    auto buf = U(u"abcdefghij1234567890");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isWordStart, 0);
    ASSERT_EQ(a.syllableLen, 16);
    // Last 16 chars of "abcdefghij1234567890" = "efghij1234567890"
    EXPECT_EQ(a.currentSyllable[0], u'e');
    EXPECT_EQ(a.currentSyllable[15], u'0');
}

TEST(HookContextAnchorTest, Derive_OnlyWhitespaceBuffer_ConservativeStart) {
    auto buf = U(u"   ");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    // Conservative: both sentence+line start true (we don't know doc position).
    EXPECT_EQ(a.isSentenceStart, 1);
    EXPECT_EQ(a.isLineStart, 1);
    EXPECT_EQ(a.isWordStart, 1);
}

TEST(HookContextAnchorTest, Derive_MidSentenceAfterComma_NoFlags) {
    auto buf = U(u"a, b");
    HookContextAnchor a{};
    DeriveAnchorFromPreceding(buf.data(), buf.size(), a);
    EXPECT_EQ(a.isSentenceStart, 0);
    EXPECT_EQ(a.isLineStart, 0);
    EXPECT_EQ(a.isWordStart, 0);
    ASSERT_EQ(a.syllableLen, 1);
    EXPECT_EQ(a.currentSyllable[0], u'b');
}

// ============================================================================
// Seqlock roundtrip + retry logic
// ============================================================================

TEST(HookContextAnchorTest, Seqlock_Roundtrip_SingleWriteRead) {
    HookContextAnchor slot{};
    slot.generation = 0;

    HookContextAnchor src{};
    src.isAvailable = 1;
    src.isSentenceStart = 1;
    src.isLineStart = 0;
    src.isWordStart = 1;
    src.syllableLen = 3;
    src.currentSyllable[0] = u'a';
    src.currentSyllable[1] = u'b';
    src.currentSyllable[2] = u'c';

    WriteAnchorSeqlock(&slot, src);

    // After write, generation must be even (stable).
    EXPECT_EQ(slot.generation & 1u, 0u);
    EXPECT_EQ(slot.generation, 2u);

    HookContextAnchor dst{};
    ASSERT_TRUE(ReadAnchorSeqlock(&slot, dst));
    EXPECT_EQ(dst.generation, 2u);
    EXPECT_EQ(dst.isAvailable, 1);
    EXPECT_EQ(dst.isSentenceStart, 1);
    EXPECT_EQ(dst.isLineStart, 0);
    EXPECT_EQ(dst.isWordStart, 1);
    EXPECT_EQ(dst.syllableLen, 3);
    EXPECT_EQ(dst.currentSyllable[0], u'a');
    EXPECT_EQ(dst.currentSyllable[1], u'b');
    EXPECT_EQ(dst.currentSyllable[2], u'c');
}

TEST(HookContextAnchorTest, Seqlock_MultipleWrites_GenerationAdvances) {
    HookContextAnchor slot{};
    HookContextAnchor src{};
    WriteAnchorSeqlock(&slot, src);
    EXPECT_EQ(slot.generation, 2u);
    WriteAnchorSeqlock(&slot, src);
    EXPECT_EQ(slot.generation, 4u);
    WriteAnchorSeqlock(&slot, src);
    EXPECT_EQ(slot.generation, 6u);
}

TEST(HookContextAnchorTest, Seqlock_ReaderDetectsWriteInProgress_ReturnsFalse) {
    HookContextAnchor slot{};
    // Simulate a writer stuck mid-write: generation is odd.
    slot.generation = 1;
    HookContextAnchor dst{};
    // Should retry 3 times and give up.
    EXPECT_FALSE(ReadAnchorSeqlock(&slot, dst));
}

TEST(HookContextAnchorTest, Seqlock_NullPointer_ReadFails) {
    HookContextAnchor dst{};
    EXPECT_FALSE(ReadAnchorSeqlock(nullptr, dst));
}

TEST(HookContextAnchorTest, Seqlock_NullPointer_WriteIsNoOp) {
    HookContextAnchor src{};
    src.isSentenceStart = 1;
    // Should not crash.
    WriteAnchorSeqlock(nullptr, src);
    SUCCEED();
}

// Concurrent stress: 1 writer, 4 readers. Writers bump data; readers must
// always observe a consistent snapshot (never a half-written struct).
TEST(HookContextAnchorTest, Seqlock_ConcurrentStress_NoTornRead) {
    HookContextAnchor slot{};
    std::atomic<bool> stop{false};
    std::atomic<int> tornReads{0};
    std::atomic<int> successfulReads{0};

    std::thread writer([&]() {
        HookContextAnchor src{};
        uint8_t counter = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            // Make all fields correlated: if reader sees torn, some won't match.
            src.isAvailable     = counter & 1;
            src.isSentenceStart = counter & 1;
            src.isLineStart     = counter & 1;
            src.isWordStart     = counter & 1;
            src.syllableLen     = counter;
            for (auto& c : src.currentSyllable) c = counter;
            WriteAnchorSeqlock(&slot, src);
            ++counter;
        }
    });

    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&]() {
            HookContextAnchor dst{};
            while (!stop.load(std::memory_order_relaxed)) {
                if (!ReadAnchorSeqlock(&slot, dst)) continue;
                successfulReads.fetch_add(1, std::memory_order_relaxed);
                // Invariant: syllableLen byte parity must match all the correlated booleans.
                uint8_t expectedBit = dst.syllableLen & 1;
                if (dst.isAvailable != expectedBit ||
                    dst.isSentenceStart != expectedBit ||
                    dst.isLineStart != expectedBit ||
                    dst.isWordStart != expectedBit) {
                    tornReads.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                // currentSyllable all equal syllableLen.
                for (auto c : dst.currentSyllable) {
                    if (c != dst.syllableLen) {
                        tornReads.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                }
            }
        });
    }

    // Run for ~100ms.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_relaxed);
    writer.join();
    for (auto& t : readers) t.join();

    EXPECT_GT(successfulReads.load(), 0);
    EXPECT_EQ(tornReads.load(), 0);
}

// ============================================================================
// Sanity: struct sizes / versioning
// ============================================================================

TEST(HookContextAnchorTest, StructSize_Is44Bytes) {
    EXPECT_EQ(sizeof(HookContextAnchor), 44u);
}

TEST(HookContextAnchorTest, SharedState_ContainsContextAnchor) {
    SharedState s{};
    s.InitDefaults();
    // Anchor starts zeroed by InitDefaults.
    EXPECT_EQ(s.contextAnchor.generation, 0u);
    EXPECT_EQ(s.contextAnchor.isAvailable, 0);
    EXPECT_EQ(s.contextAnchor.syllableLen, 0);
}

TEST(HookContextAnchorTest, SharedState_VersionAtLeastV3) {
    // v3 added contextAnchor; v4 grew reserved[] to 1024 (no anchor impact).
    // Both versions expose the same contextAnchor contract.
    EXPECT_GE(SharedState::CURRENT_VERSION, 3u);
}

TEST(HookContextAnchorTest, SharedFlags_TsfReadonlyDefined) {
    EXPECT_EQ(SharedFlags::TSF_READONLY, 0x0010u);
    // Mutually exclusive bit from TSF_ACTIVE.
    EXPECT_NE(SharedFlags::TSF_READONLY, SharedFlags::TSF_ACTIVE);
}

}  // namespace
}  // namespace NextKey
