// tests/pipeline/HookCompositionSessionTest.cpp
#include <gtest/gtest.h>
#include "core/pipeline/HookCompositionSession.h"

#include <string_view>

using namespace NextKey::Pipeline;

TEST(HookCompositionSession, ReturnsPreviousRendered) {
    HookCompositionSession s(L"hie", L"hiê", L"hie");
    EXPECT_EQ(s.PreviousRendered(), std::wstring_view{L"hie"});
}

TEST(HookCompositionSession, ReturnsEngineRendered) {
    HookCompositionSession s(L"hie", L"hiê", L"hie");
    EXPECT_EQ(s.EngineRendered(), std::wstring_view{L"hiê"});
}

TEST(HookCompositionSession, ReturnsRawInput) {
    HookCompositionSession s(L"hie", L"hiê", L"hie");
    EXPECT_EQ(s.RawInput(), std::wstring_view{L"hie"});
}
