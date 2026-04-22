// NexusKey - SharedState Unit Tests
// SPDX-License-Identifier: GPL-3.0-only

#include <gtest/gtest.h>
#include "core/ipc/SharedState.h"
#include "core/config/TypingConfig.h"

namespace NextKey {
namespace {

class SharedStateTest : public ::testing::Test {};

// ============================================================================
// SharedState Struct Tests
// ============================================================================

TEST_F(SharedStateTest, InitDefaults_SetsMagic) {
    SharedState state{};
    state.InitDefaults();

    EXPECT_EQ(state.magic, SharedState::MAGIC_VALUE);
    EXPECT_TRUE(state.IsValid());
}

TEST_F(SharedStateTest, InitDefaults_SetsDefaultValues) {
    SharedState state{};
    state.InitDefaults();

    EXPECT_EQ(state.epoch, 0u);
    EXPECT_EQ(state.inputMethod, 0);  // Telex
    EXPECT_EQ(state.spellCheck, 0);
    EXPECT_EQ(state.optimizeLevel, 0);
    EXPECT_EQ(state.GetFeatureFlags(), FeatureFlags::ALLOW_ZWJF);  // Default: tone keys enabled
}

TEST_F(SharedStateTest, InitDefaults_SetsVersioning) {
    SharedState state{};
    state.InitDefaults();

    EXPECT_EQ(state.structVersion, SharedState::CURRENT_VERSION);
    EXPECT_EQ(state.structSize, sizeof(SharedState));
}

TEST_F(SharedStateTest, InitDefaults_SetsVietnameseMode) {
    SharedState state{};
    state.InitDefaults();

    // Default flags should have Vietnamese mode enabled
    EXPECT_TRUE(state.flags & SharedFlags::VIETNAMESE_MODE);
    EXPECT_TRUE(state.flags & SharedFlags::ENGINE_ENABLED);
}

TEST_F(SharedStateTest, IsValid_FalseForUninitializedState) {
    SharedState state{};  // Zero-initialized
    EXPECT_FALSE(state.IsValid());
}

TEST_F(SharedStateTest, IsValid_FalseForWrongMagic) {
    SharedState state{};
    state.magic = 0xDEADBEEF;  // Wrong magic
    EXPECT_FALSE(state.IsValid());
}

TEST_F(SharedStateTest, IsValid_ChecksVersionBound) {
    SharedState state{};
    state.InitDefaults();

    // Valid version
    EXPECT_TRUE(state.IsValid());

    // Future version (from newer app) — should be invalid
    state.structVersion = SharedState::CURRENT_VERSION + 1;
    EXPECT_FALSE(state.IsValid());
}

TEST_F(SharedStateTest, IsValid_ChecksMinimumStructSize) {
    SharedState state{};
    state.InitDefaults();
    EXPECT_TRUE(state.IsValid());

    // Too small
    state.structSize = 10;
    EXPECT_FALSE(state.IsValid());
}

// ============================================================================
// SharedState + TypingConfig Integration Tests
// ============================================================================

TEST_F(SharedStateTest, ConfigToSharedState_Telex) {
    TypingConfig config;
    config.inputMethod = InputMethod::Telex;
    config.spellCheckEnabled = false;
    config.optimizeLevel = 0;

    SharedState state{};
    state.InitDefaults();
    state.inputMethod = static_cast<uint8_t>(config.inputMethod);
    state.spellCheck = config.spellCheckEnabled ? 1 : 0;
    state.optimizeLevel = config.optimizeLevel;

    EXPECT_EQ(state.inputMethod, 0);  // Telex = 0
    EXPECT_EQ(state.spellCheck, 0);
    EXPECT_TRUE(state.IsValid());
}

TEST_F(SharedStateTest, ConfigToSharedState_VNI) {
    TypingConfig config;
    config.inputMethod = InputMethod::VNI;
    config.spellCheckEnabled = true;
    config.optimizeLevel = 2;

    SharedState state{};
    state.InitDefaults();
    state.inputMethod = static_cast<uint8_t>(config.inputMethod);
    state.spellCheck = config.spellCheckEnabled ? 1 : 0;
    state.optimizeLevel = config.optimizeLevel;

    EXPECT_EQ(state.inputMethod, 1);  // VNI = 1
    EXPECT_EQ(state.spellCheck, 1);
    EXPECT_EQ(state.optimizeLevel, 2);
    EXPECT_TRUE(state.IsValid());
}

TEST_F(SharedStateTest, SharedStateToConfig_Telex) {
    SharedState state{};
    state.InitDefaults();
    state.inputMethod = 0;  // Telex
    state.spellCheck = 0;
    state.optimizeLevel = 1;

    // Simulate reading config from SharedState
    InputMethod method = static_cast<InputMethod>(state.inputMethod);
    bool spellCheck = state.spellCheck != 0;
    uint8_t optLevel = state.optimizeLevel;

    EXPECT_EQ(method, InputMethod::Telex);
    EXPECT_FALSE(spellCheck);
    EXPECT_EQ(optLevel, 1);
}

TEST_F(SharedStateTest, SharedStateToConfig_VNI) {
    SharedState state{};
    state.InitDefaults();
    state.inputMethod = 1;  // VNI
    state.spellCheck = 1;
    state.optimizeLevel = 0;

    InputMethod method = static_cast<InputMethod>(state.inputMethod);
    bool spellCheck = state.spellCheck != 0;

    EXPECT_EQ(method, InputMethod::VNI);
    EXPECT_TRUE(spellCheck);
}

// ============================================================================
// Epoch Tests (Seqlock & Config Change Detection)
// ============================================================================

TEST_F(SharedStateTest, Epoch_StartsAtZero) {
    SharedState state{};
    state.InitDefaults();
    EXPECT_EQ(state.epoch, 0u);
}

TEST_F(SharedStateTest, Epoch_EvenMeansStable) {
    SharedState state{};
    state.InitDefaults();

    // Epoch 0 = even = stable
    EXPECT_EQ(state.epoch & 1, 0u);
}

TEST_F(SharedStateTest, Epoch_DetectsChange) {
    SharedState state{};
    state.InitDefaults();

    uint32_t lastEpoch = state.epoch;

    // Simulate Write() auto-increment: 0 → 1 (writing) → 2 (done)
    state.epoch += 2;

    EXPECT_NE(state.epoch, lastEpoch);
    EXPECT_EQ(state.epoch & 1, 0u);  // Still even after write
}

// ============================================================================
// Reserved Space Tests
// ============================================================================

TEST_F(SharedStateTest, ReservedSpace_ZeroInitialized) {
    SharedState state{};
    state.InitDefaults();

    for (const auto& b : state.reserved) {
        EXPECT_EQ(b, 0);
    }
}

// ============================================================================
// Feature Flags Encode/Decode Roundtrip Tests
// These cover every toggle in Settings dialog.
// If a toggle appears to "not apply", the encode/decode chain is the first
// thing to check. The realtime delivery path (100ms timer in main.cpp) is
// only useful if the value actually survives the SharedState roundtrip.
// ============================================================================

TEST_F(SharedStateTest, FeatureFlags_BeepOnSwitch_Roundtrip) {
    TypingConfig cfg{};
    cfg.beepOnSwitch = true;
    SharedState state{};
    state.InitDefaults();
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));

    TypingConfig out{};
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_TRUE(out.beepOnSwitch);

    cfg.beepOnSwitch = false;
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_FALSE(out.beepOnSwitch);
}

TEST_F(SharedStateTest, FeatureFlags_MacroEnabled_Roundtrip) {
    TypingConfig cfg{};
    cfg.macroEnabled = true;
    SharedState state{};
    state.InitDefaults();
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));

    TypingConfig out{};
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_TRUE(out.macroEnabled);

    cfg.macroEnabled = false;
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_FALSE(out.macroEnabled);
}

TEST_F(SharedStateTest, FeatureFlags_MacroInEnglish_Roundtrip) {
    TypingConfig cfg{};
    cfg.macroInEnglish = true;
    SharedState state{};
    state.InitDefaults();
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));

    TypingConfig out{};
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_TRUE(out.macroInEnglish);
}

TEST_F(SharedStateTest, FeatureFlags_SmartSwitch_Roundtrip) {
    TypingConfig cfg{};
    cfg.smartSwitch = true;
    SharedState state{};
    state.InitDefaults();
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));

    TypingConfig out{};
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_TRUE(out.smartSwitch);
}

TEST_F(SharedStateTest, FeatureFlags_TempOffByAlt_Roundtrip) {
    TypingConfig cfg{};
    cfg.tempOffByAlt = true;
    SharedState state{};
    state.InitDefaults();
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));

    TypingConfig out{};
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_TRUE(out.tempOffByAlt);
}

// All toggles ON simultaneously — verifies no bit aliasing
TEST_F(SharedStateTest, FeatureFlags_AllTogglesOn_NoAliasing) {
    TypingConfig cfg{};
    cfg.beepOnSwitch       = true;
    cfg.macroEnabled       = true;
    cfg.macroInEnglish     = true;
    cfg.smartSwitch        = true;
    cfg.tempOffByAlt       = true;

    SharedState state{};
    state.InitDefaults();
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));

    TypingConfig out{};
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_TRUE(out.beepOnSwitch);
    EXPECT_TRUE(out.macroEnabled);
    EXPECT_TRUE(out.macroInEnglish);
    EXPECT_TRUE(out.smartSwitch);
    EXPECT_TRUE(out.tempOffByAlt);
}

// Toggle beep OFF while others remain ON — verifies individual bit isolation
TEST_F(SharedStateTest, FeatureFlags_ToggleOneOff_OthersUnchanged) {
    TypingConfig cfg{};
    cfg.beepOnSwitch   = true;
    cfg.macroEnabled   = true;
    cfg.smartSwitch    = true;

    SharedState state{};
    state.InitDefaults();
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));

    // Now disable beep only
    cfg.beepOnSwitch = false;
    state.SetFeatureFlags(EncodeFeatureFlags(cfg));

    TypingConfig out{};
    DecodeFeatureFlags(state.GetFeatureFlags(), out);
    EXPECT_FALSE(out.beepOnSwitch);
    EXPECT_TRUE(out.macroEnabled);
    EXPECT_TRUE(out.smartSwitch);
}

// ============================================================================
// ABI-gate tests (hybrid TSF DLL update — see docs/plans/2026-04-22-tsf-*)
// ============================================================================

TEST_F(SharedStateTest, ReservedPool_IsAtLeast1024Bytes) {
    SharedState state{};
    EXPECT_GE(sizeof(state.reserved), 1024u);
}

TEST_F(SharedStateTest, CurrentVersion_IsAtLeast4) {
    EXPECT_GE(SharedState::CURRENT_VERSION, 4u);
}

TEST_F(SharedStateTest, FutureVersion_FailsIsValid_SoDllCanDetectMismatch) {
    // Simulate: new EXE wrote a struct with structVersion = CURRENT_VERSION + 1.
    // An old DLL reading this memory must see IsValid() == false so it can
    // disable itself and set TSF_ABI_MISMATCH.
    SharedState state{};
    state.InitDefaults();
    state.structVersion = SharedState::CURRENT_VERSION + 1;

    EXPECT_FALSE(state.IsValid())
        << "ABI gate relies on IsValid() rejecting future versions";
}

TEST_F(SharedStateTest, ShrunkStruct_FailsIsValid_SoDllCanDetectMismatch) {
    // Simulate: writer claimed a struct smaller than the header itself.
    SharedState state{};
    state.InitDefaults();
    state.structSize = 20;  // < 24 (minimum: header + epoch + flags + config)

    EXPECT_FALSE(state.IsValid());
}

TEST_F(SharedStateTest, AbiHeaderFields_DoNotOverlapEpoch) {
    // The DLL's IsAbiCompatible() reads magic/structVersion/structSize without
    // seqlock protection, relying on the fact that these three fields are
    // written once at Create() and never mutated. If anyone ever adds an
    // InitDefaults-style writer that touches them after creation, this test
    // becomes a red flag for the ABI gate.
    EXPECT_EQ(offsetof(SharedState, magic), 0u);
    EXPECT_EQ(offsetof(SharedState, structVersion), 4u);
    EXPECT_EQ(offsetof(SharedState, structSize), 8u);
    // epoch is the first seqlock-managed field — ABI header must end before it.
    EXPECT_EQ(offsetof(SharedState, epoch), 12u);
}

TEST_F(SharedStateTest, SharedFlags_NewUpdateBitsDoNotCollide) {
    // Sanity: each new flag is non-zero and does not collide with existing bits
    // or with each other.
    constexpr uint32_t existing =
        SharedFlags::VIETNAMESE_MODE |
        SharedFlags::ENGINE_ENABLED  |
        SharedFlags::SPELL_CHECK     |
        SharedFlags::TSF_ACTIVE      |
        SharedFlags::TSF_READONLY;
    EXPECT_NE(SharedFlags::TSF_ABI_MISMATCH, 0u);
    EXPECT_NE(SharedFlags::TSF_PENDING_DLL_SWAP, 0u);
    EXPECT_NE(SharedFlags::TSF_POST_UPDATE_REBOOT, 0u);
    EXPECT_EQ(SharedFlags::TSF_ABI_MISMATCH & existing, 0u);
    EXPECT_EQ(SharedFlags::TSF_PENDING_DLL_SWAP & existing, 0u);
    EXPECT_EQ(SharedFlags::TSF_POST_UPDATE_REBOOT & existing, 0u);
    EXPECT_EQ(SharedFlags::TSF_ABI_MISMATCH & SharedFlags::TSF_PENDING_DLL_SWAP, 0u);
    EXPECT_EQ(SharedFlags::TSF_ABI_MISMATCH & SharedFlags::TSF_POST_UPDATE_REBOOT, 0u);
    EXPECT_EQ(SharedFlags::TSF_PENDING_DLL_SWAP & SharedFlags::TSF_POST_UPDATE_REBOOT, 0u);
}

}  // namespace
}  // namespace NextKey
