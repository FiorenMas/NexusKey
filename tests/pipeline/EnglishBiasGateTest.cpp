// tests/pipeline/EnglishBiasGateTest.cpp
//
// W2.2 — verify EnglishBiasGate reads an external std::atomic<bool> vnMode
// reference and reports IsRaised() == true iff vnMode == false (English mode).
#include <gtest/gtest.h>

#include <atomic>
#include <string_view>

#include "core/pipeline/ICompositionSession.h"
#include "core/pipeline/KeyContext.h"
#include "core/pipeline/gates/EnglishBiasGate.h"

using namespace NextKey::Pipeline;

namespace {

class StubSession final : public ICompositionSession {
public:
    [[nodiscard]] std::wstring_view PreviousRendered() const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view EngineRendered()   const noexcept override { return {}; }
    [[nodiscard]] std::wstring_view RawInput()         const noexcept override { return {}; }
};

KeyContext makeStubCtx(const ICompositionSession& session) {
    return KeyContext{
        .vk = 0x41, .keyChar = L'a',
        .shift = false, .capsLock = false, .ctrl = false, .alt = false, .win = false,
        .session = &session, .reinjectVk = 0,
    };
}

}  // namespace

TEST(EnglishBiasGate, NotRaisedWhenVnModeTrue) {
    std::atomic<bool> vn{true};
    EnglishBiasGate g(vn);
    StubSession session;
    EXPECT_FALSE(g.IsRaised(makeStubCtx(session)));
}

TEST(EnglishBiasGate, RaisedWhenVnModeFalse) {
    std::atomic<bool> vn{false};
    EnglishBiasGate g(vn);
    StubSession session;
    EXPECT_TRUE(g.IsRaised(makeStubCtx(session)));
}

TEST(EnglishBiasGate, IdIsEnglishBias) {
    std::atomic<bool> vn{true};
    EnglishBiasGate g(vn);
    EXPECT_EQ(g.Id(), GateId::EnglishBias);
}
