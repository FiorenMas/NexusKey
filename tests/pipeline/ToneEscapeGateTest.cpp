// tests/pipeline/ToneEscapeGateTest.cpp
//
// W5.2 — verify ToneEscapeGate reads IInputEngine::IsToneEscaped() via a
// std::unique_ptr<IInputEngine>& reference and reports IsRaised iff the
// engine has any active EscapeKind.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

#include "core/engine/IInputEngine.h"
#include "core/pipeline/ICompositionSession.h"
#include "core/pipeline/KeyContext.h"
#include "core/pipeline/gates/ToneEscapeGate.h"

using namespace NextKey::Pipeline;

namespace {

class MockEngine final : public NextKey::IInputEngine {
public:
    bool english_ = false;
    bool escaped_ = false;
    std::wstring buf_;
    void   PushChar(wchar_t) override {}
    void   Backspace() override {}
    [[nodiscard]] const std::wstring& Peek() const override { return buf_; }
    [[nodiscard]] std::wstring Commit() override { return {}; }
    void   Reset() override {}
    [[nodiscard]] size_t Count() const override { return 0; }
    [[nodiscard]] bool   HasActiveQuickConsonant() const override { return false; }
    [[nodiscard]] bool   SeedFromText(const std::wstring&) override { return false; }
    [[nodiscard]] bool   IsEnglishWord() const override { return english_; }
    [[nodiscard]] bool   IsToneEscaped() const override { return escaped_; }
};

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

TEST(ToneEscapeGate, NotRaisedWhenEngineNotEscaped) {
    std::unique_ptr<NextKey::IInputEngine> engine = std::make_unique<MockEngine>();
    static_cast<MockEngine*>(engine.get())->escaped_ = false;
    ToneEscapeGate g(engine);
    StubSession session;
    EXPECT_FALSE(g.IsRaised(makeStubCtx(session)));
}

TEST(ToneEscapeGate, RaisedWhenEngineEscaped) {
    std::unique_ptr<NextKey::IInputEngine> engine = std::make_unique<MockEngine>();
    static_cast<MockEngine*>(engine.get())->escaped_ = true;
    ToneEscapeGate g(engine);
    StubSession session;
    EXPECT_TRUE(g.IsRaised(makeStubCtx(session)));
}

TEST(ToneEscapeGate, NotRaisedWhenEngineNull) {
    std::unique_ptr<NextKey::IInputEngine> engine;  // nullptr
    ToneEscapeGate g(engine);
    StubSession session;
    EXPECT_FALSE(g.IsRaised(makeStubCtx(session)));
}

TEST(ToneEscapeGate, IdIsToneEscape) {
    std::unique_ptr<NextKey::IInputEngine> engine = std::make_unique<MockEngine>();
    ToneEscapeGate g(engine);
    EXPECT_EQ(g.Id(), GateId::ToneEscape);
}

TEST(ToneEscapeGate, RereadsEngineEachCall) {
    std::unique_ptr<NextKey::IInputEngine> engine = std::make_unique<MockEngine>();
    auto* mock = static_cast<MockEngine*>(engine.get());
    ToneEscapeGate g(engine);
    StubSession session;
    mock->escaped_ = false;
    EXPECT_FALSE(g.IsRaised(makeStubCtx(session)));
    mock->escaped_ = true;
    EXPECT_TRUE(g.IsRaised(makeStubCtx(session)));
    mock->escaped_ = false;
    EXPECT_FALSE(g.IsRaised(makeStubCtx(session)));
}
