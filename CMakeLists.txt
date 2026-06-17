cmake_minimum_required(VERSION 3.20)
project(VKey VERSION 4.1.0 LANGUAGES CXX)

# Generate Version.h from Version.h.in — version is defined once in project() above.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Version.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/Version.h
    @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows specific configurations
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
    add_definitions(-DUNICODE -D_UNICODE)
    # Size shrink. Most flags here are already implicit in MSVC's /O2 Release
    # path (/Gy is implied by /O2; /OPT:REF and /OPT:ICF are on by default when
    # /INCREMENTAL:NO is set, which CMake Release defaults to). We restate them
    # explicitly so a future tweak to release flags doesn't silently turn them
    # off. The actual NEW flag here is:
    #   /Gw  — data-level linking: each global var in its own COMDAT so
    #          /OPT:REF can strip unreferenced const tables and /OPT:ICF can
    #          fold identical ones (template-emitted strings, vtables, etc.).
    #          NOT implied by /O2 and saves measurable .rdata.
    add_compile_options($<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Gw>)
    # Linker security flags: ASLR, DEP, High-Entropy ASLR
    add_link_options(/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
    # PE legitimacy: checksum (reduces AV false positives)
    add_link_options(/RELEASE)
    add_link_options($<$<CONFIG:Release>:/INCREMENTAL:NO>
                     $<$<CONFIG:Release>:/OPT:REF>
                     $<$<CONFIG:Release>:/OPT:ICF>)
endif()

# Per-target include directories (no global include_directories)

# Engine Library (pure C++17, zero platform dependencies)
set(NEXTKEY_ENGINE_SOURCES
    src/core/config/TypingConfig.h
    src/core/config/ConfigSnapshot.h
    src/core/engine/TypingEngine.cpp
    src/core/engine/TypingEngine.h
    src/core/engine/EngineFactory.cpp
    src/core/engine/EngineFactory.h
    src/core/engine/IInputEngine.h
    src/core/engine/EngineHelpers.h
    src/core/engine/EnglishProtection.h
    src/core/engine/VietnameseTables.h
    src/core/engine/VietnamesePhonologyData.h
    src/core/engine/IPhonologyRules.h
    src/core/engine/DefaultPhonologyRules.h
    src/core/engine/PhonologyRulePackFactory.h
    src/core/engine/PhonotacticsValidator.cpp
    src/core/engine/PhonotacticsValidator.h
    src/core/engine/IPhonotactics.h
    src/core/engine/Phonotactics.cpp
    src/core/engine/Phonotactics.h
    src/core/engine/TypingAction.h
    src/core/engine/CodeTableConverter.cpp
    src/core/engine/CodeTableConverter.h
    src/core/engine/rule/EngineRulePhase.h
    src/core/engine/rule/EngineRuleResult.h
    src/core/engine/rule/EngineRuleContext.h
    src/core/engine/rule/IEngineRule.h
    src/core/engine/rule/EngineRuleRegistry.h
    src/core/engine/rule/EngineRuleRegistry.cpp
    src/core/engine/rule/IToneExecutor.h
    src/core/engine/rule/ToneRule.h
    src/core/engine/rule/ToneRule.cpp
    src/core/engine/rule/IModifierExecutor.h
    src/core/engine/rule/ModifierRule.h
    src/core/engine/rule/ModifierRule.cpp
    src/core/engine/rule/IModifierSubExecutor.h
    src/core/engine/rule/ModifierProposal.h
    src/core/engine/rule/AdjacentCircumflexProposal.h
    src/core/engine/rule/AdjacentCircumflexProposal.cpp
    src/core/engine/rule/HornModifierProposal.h
    src/core/engine/rule/HornModifierProposal.cpp
    src/core/engine/rule/StrokeDProposal.h
    src/core/engine/rule/StrokeDProposal.cpp
    src/core/engine/rule/BracketProposal.h
    src/core/engine/rule/BracketProposal.cpp
    src/core/engine/rule/VniCircumflexProposal.h
    src/core/engine/rule/VniCircumflexProposal.cpp
    src/core/engine/rule/VniHornProposal.h
    src/core/engine/rule/VniHornProposal.cpp
    src/core/engine/rule/VniBreveProposal.h
    src/core/engine/rule/VniBreveProposal.cpp
    src/core/engine/rule/IQuickConsonantExecutor.h
    src/core/engine/rule/QuickStartConsonantRule.h
    src/core/engine/rule/QuickStartConsonantRule.cpp
    src/core/engine/rule/QuickEndConsonantRule.h
    src/core/engine/rule/QuickEndConsonantRule.cpp
)

add_library(VKeyEngine STATIC ${NEXTKEY_ENGINE_SOURCES})
target_include_directories(VKeyEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Core Library (platform layer: SharedState, Config, Debug)
set(NEXTKEY_CORE_SOURCES
    src/core/Debug.h
    src/core/Logger.h
    src/core/Logger.cpp
    src/core/CrashLog.h
    src/core/CrashLog.cpp
    src/core/Strings.h
    src/core/Strings.cpp
    src/core/MacroCase.h
    src/core/MacroCase.cpp
    src/core/hotkey/HotkeyRegistry.h
    src/core/hotkey/HotkeyRegistry.cpp
    src/core/hotkey/HotkeyLabel.h
    src/core/hotkey/HotkeyLabel.cpp
    src/core/ipc/SharedState.h
    src/core/ipc/SharedConstants.h
    src/core/pipeline/Stage.h
    src/core/pipeline/Result.h
    src/core/pipeline/GateMask.h
    src/core/pipeline/Intent.h
    src/core/pipeline/IntentSink.h
    src/core/pipeline/ICompositionSession.h
    src/core/pipeline/HookCompositionSession.h
    src/core/pipeline/KeyContext.h
    src/core/pipeline/IFeature.h
    src/core/pipeline/IGate.h
    src/core/pipeline/IBackwardEditExecutor.h
    src/core/pipeline/BackwardEditFeature.h
    src/core/pipeline/BackwardEditFeature.cpp
    src/core/pipeline/ICommitUndoExecutor.h
    src/core/pipeline/CommitUndoFeature.h
    src/core/pipeline/CommitUndoFeature.cpp
    src/core/pipeline/IEscRestoreRawExecutor.h
    src/core/pipeline/EscRestoreRawFeature.h
    src/core/pipeline/EscRestoreRawFeature.cpp
    src/core/pipeline/IMacroExecutor.h
    src/core/pipeline/MacroFeature.h
    src/core/pipeline/MacroFeature.cpp
    src/core/pipeline/gates/EnglishBiasGate.h
    src/core/pipeline/gates/SpellCheckGate.h
    src/core/pipeline/gates/ToneEscapeGate.h
    src/core/pipeline/Coordinator.h
    src/core/pipeline/Coordinator.cpp
    src/core/pipeline/OutputChannel.h
    src/core/pipeline/OutputChannel.cpp
)

# Windows-only sources (require Windows.h)
if(WIN32)
    list(APPEND NEXTKEY_CORE_SOURCES
        src/core/ipc/SharedStateManager.cpp
        src/core/ipc/SharedStateManager.h
        src/core/SmartSwitchManager.cpp
        src/core/SmartSwitchManager.h
        src/core/SmartSwitchState.h
        src/core/config/ConfigEvent.cpp
        src/core/config/ConfigEvent.h
        src/core/config/ConfigManager.cpp
        src/core/config/ConfigManager.h
        src/core/config/ConfigSnapshotBuilder.cpp
        src/core/config/ConfigSnapshotBuilder.h
    )
endif()

add_library(VKeyCore STATIC ${NEXTKEY_CORE_SOURCES})
target_include_directories(VKeyCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/tomlplusplus
)
target_link_libraries(VKeyCore PUBLIC VKeyEngine)

# TSF DLL (TIP)
add_library(VKeyTSF SHARED
    src/tsf/stdafx.h
    src/tsf/Globals.h
    src/tsf/Globals.cpp
    src/tsf/TextService.h
    src/tsf/TextService.cpp
    src/tsf/KeyEventSink.h
    src/tsf/KeyEventSink.cpp
    src/tsf/CompositionManager.h
    src/tsf/CompositionManager.cpp
    src/tsf/EngineController.h
    src/tsf/EngineController.cpp
    src/tsf/ReadonlyContextProvider.h
    src/tsf/ReadonlyContextProvider.cpp
    src/tsf/Register.cpp
    src/tsf/dllmain.cpp
    src/tsf/EditSession.h
    src/tsf/CompositionEditSession.h
    src/tsf/EscRestoreLastCommitSession.h
    src/tsf/InputScopeChecker.h
    src/tsf/LanguageBarButton.h
    src/tsf/LanguageBarButton.cpp
    src/tsf/DisplayAttribute.h
    src/tsf/ComUtils.h
    src/tsf/Define.h
    src/tsf/resource.h
    src/tsf/VKeyTSF.rc
    src/tsf/VKeyTSF.def
)
target_link_libraries(VKeyTSF PRIVATE VKeyCore)
set_target_properties(VKeyTSF PROPERTIES 
    LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/src/tsf/VKeyTSF.def\""
)

# Core App (EXE) - Win32 GUI application with Sciter UI
add_executable(VKeyApp WIN32
    src/app/main.cpp
    src/app/resource.h
    src/app/VKey.rc
    # dialogs/
    src/app/dialogs/SciterSubDialog.h
    src/app/dialogs/SciterSubDialog.cpp
    src/app/dialogs/SubDialogConfig.h
    src/app/dialogs/SettingsDialog.h
    src/app/dialogs/SettingsDialog.cpp
    src/app/dialogs/ExcludedAppsDialog.h
    src/app/dialogs/ExcludedAppsDialog.cpp
    src/app/dialogs/TsfAppsDialog.h
    src/app/dialogs/TsfAppsDialog.cpp
    src/app/dialogs/MacroTableDialog.h
    src/app/dialogs/MacroTableDialog.cpp
    src/app/dialogs/ConvertToolDialog.h
    src/app/dialogs/ConvertToolDialog.cpp
    src/app/dialogs/AboutDialog.h
    src/app/dialogs/AboutDialog.cpp
    src/app/dialogs/WindowPickerDialog.h
    src/app/dialogs/WindowPickerDialog.cpp
    src/app/dialogs/AppOverridesDialog.h
    src/app/dialogs/AppOverridesDialog.cpp
    src/app/dialogs/SpellExclusionsDialog.h
    src/app/dialogs/SpellExclusionsDialog.cpp
    src/app/dialogs/UserDefinedDialog.h
    src/app/dialogs/UserDefinedDialog.cpp
    src/app/dialogs/HotkeysDialog.h
    src/app/dialogs/HotkeysDialog.cpp
    # system/
    src/app/system/TrayIcon.h
    src/app/system/TrayIcon.cpp
    src/app/system/FloatingIcon.h
    src/app/system/FloatingIcon.cpp
    src/app/system/HookEngine.h
    src/app/system/HookEngine.cpp
    src/app/system/HookLifecycle.h
    src/app/system/HookLifecycle.cpp
    src/app/system/HookHijackDetector.h
    src/app/system/HookHijackDetector.cpp
    src/app/system/ReinstallBurstScheduler.h
    src/app/system/ReinstallBurstScheduler.cpp
    src/app/system/FocusOwner.h
    src/app/system/FocusOwner.cpp
    src/app/system/OutputDispatcher.h
    src/app/system/OutputDispatcher.cpp
    src/app/system/CommitState.h
    src/app/system/HookCommandMailbox.h
    src/app/system/HookCommandMailbox.cpp
    src/app/system/HeartbeatPublisher.h
    src/app/system/HeartbeatPublisher.cpp
    src/app/system/WatchdogController.h
    src/app/system/WatchdogController.cpp
    src/app/system/MainThreadWorker.h
    src/app/system/MainThreadWorker.cpp
    src/app/system/HotkeyManager.h
    src/app/system/HotkeyManager.cpp
    src/app/system/HotkeyWiring.h
    src/app/system/HotkeyWiring.cpp
    src/app/system/QuickConvert.h
    src/app/system/QuickConvert.cpp
    src/app/system/ToastPopup.h
    src/app/system/ToastPopup.cpp
    src/app/system/UpdateChecker.h
    src/app/system/UpdateChecker.cpp
    src/app/system/UpdateInstaller.h
    src/app/system/UpdateInstaller.cpp
    src/app/system/UpdateSecurity.h
    src/app/system/UpdateSecurity.cpp
    src/app/system/PendingDllApply.h
    src/app/system/PendingDllApply.cpp
    src/app/system/PerfHistogram.h
    src/app/system/PerfHistogram.cpp
    src/app/system/StartupHelper.h
    src/app/system/SubprocessHelper.h
    src/app/system/SubprocessRunners.h
    src/app/system/SubprocessRunners.cpp
    src/app/system/TsfRegistration.h
    src/app/system/TsfRegistration.cpp
    # output/ — IOutputInjector strategy (Sprint 2 T3)
    src/app/output/IOutputInjector.h
    src/app/output/Internal.h
    src/app/output/Internal.cpp
    src/app/output/OutputInjectorFactory.h
    src/app/output/OutputInjectorFactory.cpp
    src/app/output/Win32SendInputInjector.h
    src/app/output/Win32SendInputInjector.cpp
    src/app/output/ClipboardInjector.cpp
    src/app/output/RichEditEmReplaceSelInjector.h
    src/app/output/RichEditEmReplaceSelInjector.cpp
    src/app/output/SplitDispatchInjector.h
    src/app/output/SplitDispatchInjector.cpp
    # sciter/
    src/app/sciter/SciterArchive.h
    src/app/sciter/SciterArchive.cpp
    src/app/sciter/SciterHelper.h
    src/app/sciter/SciterHelper.cpp
    src/app/sciter/ScaleHelper.h
    # system/ (dark mode utils, shared with LITE_MODE)
    src/app/system/DarkModeHelper.h
    src/app/system/DarkModeHelper.cpp
    # other
    src/core/Version.h
    extern/sciter/include/sciter-main.cpp
    src/app/VKey.exe.manifest
)
target_include_directories(VKeyApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/sciter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/tomlplusplus
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/system
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/sciter
)
target_compile_definitions(VKeyApp PRIVATE SKIP_MAIN VKEY_HOOK_ENGINE VKEY_PERF_HIST $<$<CONFIG:Debug>:NEXTKEY_DEBUG>)
target_link_libraries(VKeyApp PRIVATE VKeyCore)
set_target_properties(VKeyApp PROPERTIES OUTPUT_NAME "VKey")
if(MSVC)
    target_link_options(VKeyApp PRIVATE /SUBSYSTEM:WINDOWS,6.01)
endif()

# ============================================================================
# Classic UI (Lite build) — Win32 native, no Sciter dependency
# Build with: cmake -B build -DVKEY_LITE_MODE=ON
# ============================================================================
option(VKEY_LITE_MODE "Build Classic (Win32 native) UI instead of Sciter" OFF)

if(VKEY_LITE_MODE AND WIN32)
    add_executable(VKeyLite WIN32
        src/app/main_lite.cpp
        src/app/classic/resource.h
        src/app/classic/VKeyLite.rc
        src/app/classic/VKeyLite.exe.manifest
        # system/ (shared with main app)
        src/app/system/TrayIcon.h
        src/app/system/TrayIcon.cpp
        src/app/system/FloatingIcon.h
        src/app/system/FloatingIcon.cpp
        src/app/system/HookEngine.h
        src/app/system/HookEngine.cpp
        src/app/system/HookLifecycle.h
        src/app/system/HookLifecycle.cpp
        src/app/system/HookHijackDetector.h
        src/app/system/HookHijackDetector.cpp
        src/app/system/ReinstallBurstScheduler.h
        src/app/system/ReinstallBurstScheduler.cpp
        src/app/system/FocusOwner.h
        src/app/system/FocusOwner.cpp
        src/app/system/OutputDispatcher.h
        src/app/system/OutputDispatcher.cpp
        src/app/system/CommitState.h
        src/app/system/HeartbeatPublisher.h
        src/app/system/HeartbeatPublisher.cpp
        src/app/system/WatchdogController.h
        src/app/system/WatchdogController.cpp
        src/app/system/MainThreadWorker.h
        src/app/system/MainThreadWorker.cpp
        src/app/system/HotkeyManager.h
        src/app/system/HotkeyManager.cpp
        src/app/system/HotkeyWiring.h
        src/app/system/HotkeyWiring.cpp
        src/app/system/QuickConvert.h
        src/app/system/QuickConvert.cpp
        src/app/system/ToastPopup.h
        src/app/system/ToastPopup.cpp
        src/app/system/UpdateChecker.h
        src/app/system/UpdateChecker.cpp
        src/app/system/UpdateInstaller.h
        src/app/system/UpdateInstaller.cpp
        src/app/system/UpdateSecurity.h
        src/app/system/UpdateSecurity.cpp
        src/app/system/PendingDllApply.h
        src/app/system/PendingDllApply.cpp
        src/app/system/PerfHistogram.h
        src/app/system/PerfHistogram.cpp
        src/app/system/HookCommandMailbox.h
        src/app/system/HookCommandMailbox.cpp
        src/app/system/StartupHelper.h
        src/app/system/SubprocessHelper.h
        src/app/system/TsfRegistration.h
        src/app/system/TsfRegistration.cpp
        # output/ — IOutputInjector strategy (Sprint 2 T3)
        src/app/output/IOutputInjector.h
        src/app/output/Internal.h
        src/app/output/Internal.cpp
        src/app/output/OutputInjectorFactory.h
        src/app/output/OutputInjectorFactory.cpp
        src/app/output/Win32SendInputInjector.h
        src/app/output/Win32SendInputInjector.cpp
        src/app/output/ClipboardInjector.cpp
        src/app/output/RichEditEmReplaceSelInjector.h
        src/app/output/RichEditEmReplaceSelInjector.cpp
        src/app/output/SplitDispatchInjector.h
        src/app/output/SplitDispatchInjector.cpp
        # DarkModeHelper (pure Win32 dark mode utils, needed by TrayIcon/ToastPopup)
        src/app/system/DarkModeHelper.h
        src/app/system/DarkModeHelper.cpp
        # classic/ (new)
        src/app/classic/ClassicDialogUtils.h
        src/app/classic/ClassicTheme.h
        src/app/classic/ClassicTheme.cpp
        src/app/classic/ClassicSettingsDialog.h
        src/app/classic/ClassicSettingsDialog.cpp
        src/app/classic/ClassicExcludedAppsDialog.h
        src/app/classic/ClassicExcludedAppsDialog.cpp
        src/app/classic/ClassicTsfAppsDialog.h
        src/app/classic/ClassicTsfAppsDialog.cpp
        src/app/classic/ClassicMacroTableDialog.h
        src/app/classic/ClassicMacroTableDialog.cpp
        src/app/classic/ClassicHotkeysDialog.h
        src/app/classic/ClassicHotkeysDialog.cpp
        src/app/classic/ClassicHotkeyCapture.h
        src/app/classic/ClassicHotkeyCapture.cpp
        src/app/classic/ClassicAppOverridesDialog.h
        src/app/classic/ClassicAppOverridesDialog.cpp
        src/app/classic/ClassicConvertToolDialog.h
        src/app/classic/ClassicConvertToolDialog.cpp
        src/app/classic/ClassicIconColorDialog.h
        src/app/classic/ClassicIconColorDialog.cpp
        src/app/classic/ClassicSpellExclusionsDialog.h
        src/app/classic/ClassicSpellExclusionsDialog.cpp
        src/app/classic/ClassicUserDefinedDialog.h
        src/app/classic/ClassicUserDefinedDialog.cpp
        # version
        src/core/Version.h
    )
    target_include_directories(VKeyLite PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/tomlplusplus
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/classic
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/system
    )
    target_compile_definitions(VKeyLite PRIVATE
        VKEY_HOOK_ENGINE
        VKEY_LITE_MODE
        VKEY_PERF_HIST
        $<$<CONFIG:Debug>:NEXTKEY_DEBUG>
    )
    target_link_libraries(VKeyLite PRIVATE
        VKeyCore
        advapi32 user32 ole32 shell32 dwmapi uuid urlmon version comctl32 winmm bcrypt gdiplus
    )
    set_target_properties(VKeyLite PROPERTIES OUTPUT_NAME "VKeyClassic")
    if(MSVC)
        target_link_options(VKeyLite PRIVATE /SUBSYSTEM:WINDOWS,6.01)
    endif()
endif()

# Link Windows libraries
if(WIN32)
    target_link_libraries(VKeyTSF PRIVATE advapi32 user32 ole32 oleaut32 uuid shell32)
    target_link_libraries(VKeyApp PRIVATE advapi32 user32 ole32 shell32 dwmapi uuid urlmon version comctl32 winmm bcrypt)

    # Copy sciter.dll to output directory
    add_custom_command(TARGET VKeyApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/extern/sciter/bin/sciter.dll"
            "$<TARGET_FILE_DIR:VKeyApp>/sciter.dll"
        COMMENT "Copying sciter.dll to output directory"
    )


endif()

# ============================================================================
# Sciter UI Resources
# Release: Embed UI files into exe using packfolder
# Debug: Copy UI files to output directory for easy editing
# ============================================================================

# Packfolder is included in extern/sciter/bin/
set(PACKFOLDER_EXE "${CMAKE_CURRENT_SOURCE_DIR}/extern/sciter/bin/packfolder.exe")

# Check if packfolder exists
if(EXISTS "${PACKFOLDER_EXE}")
    set(PACKFOLDER_AVAILABLE TRUE)
    message(STATUS "Sciter: packfolder.exe found at ${PACKFOLDER_EXE}")
else()
    set(PACKFOLDER_AVAILABLE FALSE)
    message(STATUS "Sciter: packfolder.exe not found, using file copy mode")
endif()

# Generate resources.cpp from UI files (run before build if UI changed)
set(RESOURCES_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/app/resources.cpp")
set(UI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/app/ui")

if(PACKFOLDER_AVAILABLE)
    # Custom command to generate resources.cpp
    add_custom_command(
        OUTPUT "${RESOURCES_CPP}"
        COMMAND "${PACKFOLDER_EXE}" "${UI_SOURCE_DIR}" "${RESOURCES_CPP}" -v resources
        DEPENDS ${UI_SOURCE_DIR}/settings/settings.html
                ${UI_SOURCE_DIR}/settings/settings.css
                ${UI_SOURCE_DIR}/settings/settings.js
                ${UI_SOURCE_DIR}/excludedapps/excludedapps.html
                ${UI_SOURCE_DIR}/excludedapps/excludedapps.css
                ${UI_SOURCE_DIR}/excludedapps/excludedapps.js
                ${UI_SOURCE_DIR}/tsfapps/tsfapps.html
                ${UI_SOURCE_DIR}/tsfapps/tsfapps.css
                ${UI_SOURCE_DIR}/tsfapps/tsfapps.js
                ${UI_SOURCE_DIR}/macro/macro.html
                ${UI_SOURCE_DIR}/macro/macro.css
                ${UI_SOURCE_DIR}/macro/macro.js
                ${UI_SOURCE_DIR}/appoverrides/appoverrides.html
                ${UI_SOURCE_DIR}/appoverrides/appoverrides.css
                ${UI_SOURCE_DIR}/appoverrides/appoverrides.js
                ${UI_SOURCE_DIR}/convert-tool/convert-tool.html
                ${UI_SOURCE_DIR}/convert-tool/convert-tool.css
                ${UI_SOURCE_DIR}/convert-tool/convert-tool.js
                ${UI_SOURCE_DIR}/about/about.html
                ${UI_SOURCE_DIR}/about/about.css
                ${UI_SOURCE_DIR}/userdefined/userdefined.html
                ${UI_SOURCE_DIR}/userdefined/userdefined.css
                ${UI_SOURCE_DIR}/userdefined/userdefined.js
                ${UI_SOURCE_DIR}/hotkeys/hotkeys.html
                ${UI_SOURCE_DIR}/hotkeys/hotkeys.css
                ${UI_SOURCE_DIR}/hotkeys/hotkeys.js
                ${UI_SOURCE_DIR}/shared/utils.js
                ${UI_SOURCE_DIR}/shared/strings.js
                ${UI_SOURCE_DIR}/shared/i18n.js
                ${UI_SOURCE_DIR}/shared/theme.css
                ${UI_SOURCE_DIR}/shared/dropdown.css
        COMMENT "Packing UI resources into resources.cpp"
        VERBATIM
    )

    # Custom target to ensure resources are generated
    add_custom_target(PackUIResources DEPENDS "${RESOURCES_CPP}")
    add_dependencies(VKeyApp PackUIResources)

    # Enable packfolder for Release, file copy for Debug
    target_compile_definitions(VKeyApp PRIVATE
        $<$<CONFIG:Release>:SCITER_USE_PACKFOLDER>
        $<$<CONFIG:RelWithDebInfo>:SCITER_USE_PACKFOLDER>
    )
    message(STATUS "Sciter: Release=embedded resources, Debug=file copy")
endif()

# Always copy UI files for Debug builds (allows live editing)
add_custom_command(TARGET VKeyApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Debug>,copy_directory,true>
        "${UI_SOURCE_DIR}"
        "$<TARGET_FILE_DIR:VKeyApp>/ui"
    COMMENT "Copying UI files (Debug only)"
)

# ============================================================================
# Testing with Google Test (vendored in extern/)
# ============================================================================
# Google Test must be manually placed in extern/googletest/
# Download from: https://github.com/google/googletest/releases/tag/v1.14.0
# Extract googletest-1.14.0 folder contents to extern/googletest/

set(GOOGLETEST_VERSION "1.14.0" CACHE STRING "Google Test version")
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(extern/googletest EXCLUDE_FROM_ALL)

enable_testing()

# Tests are excluded from ALL - build explicitly with --target VKeyTests
set(NEXTKEY_TEST_SOURCES
    tests/TelexEngineTest.cpp
    tests/FormulaSegmentDecisionTest.cpp
    tests/CombinedEngineTest.cpp
    tests/VniParityTest.cpp
    tests/TelexDictionaryTest.cpp
    tests/EngineFactoryTest.cpp
    tests/SharedStateTest.cpp
    tests/HookContextAnchorTest.cpp
    tests/FeatureOptionsTest.cpp
    tests/EnglishBypassTest.cpp
    tests/PhonotacticsValidatorTest.cpp
    tests/PhonotacticsTest.cpp
    tests/TypingActionTest.cpp
    tests/CustomKeyMapTest.cpp
    tests/CodeTableConverterTest.cpp
    tests/UpdateSecurityTest.cpp
    tests/EngineBenchmarkTest.cpp
    tests/MacroPrefixTest.cpp
    tests/MacroCaseTest.cpp
    tests/HookEngineAtomicTests.cpp
    tests/TypingConfigRCUTests.cpp
    tests/MainThreadWorkerTests.cpp
    tests/AdaptiveTickTest.cpp
    tests/AdaptiveTickBenchmark.cpp
    tests/AutoCapDecisionTest.cpp
    tests/AutoCapStateTransitionTest.cpp
    tests/CjkSwitchDecisionTest.cpp
    tests/PerAppModeDecisionTest.cpp
    tests/CommitUndoExemptionTest.cpp
    tests/DigitLedWordDecisionTest.cpp
    tests/LoggerTest.cpp
    tests/HotkeyRegistryTest.cpp
    tests/PerfHistogramTest.cpp
    tests/HookCommandMailboxTest.cpp
    tests/HookHijackDetectorTest.cpp
    tests/ReinstallBurstSchedulerTest.cpp
    tests/FocusInterleavingTest.cpp
    tests/ThreadOwnershipTest.cpp
    tests/ConfigSnapshotTest.cpp
    tests/ConfigReloadBurstTest.cpp
    tests/InjectorSwitchTest.cpp
    tests/HotkeyLabelTest.cpp
    tests/HotkeyConfigMigrationTest.cpp
    tests/ParseConfigLinesTest.cpp
    tests/pipeline/EnumsTest.cpp
    tests/pipeline/IntentTest.cpp
    tests/pipeline/IntentSinkTest.cpp
    tests/pipeline/CoordinatorRegistryTest.cpp
    tests/pipeline/CoordinatorDispatchTest.cpp
    tests/pipeline/CoordinatorStageDispatchTest.cpp
    tests/pipeline/CoordinatorGateFilterTest.cpp
    tests/pipeline/OutputChannelTest.cpp
    tests/pipeline/HookCompositionSessionTest.cpp
    tests/pipeline/EnglishBiasGateTest.cpp
    tests/pipeline/SpellCheckGateTest.cpp
    tests/pipeline/ToneEscapeGateTest.cpp
    tests/pipeline/BackwardEditFeatureTest.cpp
    tests/pipeline/CommitUndoFeatureTest.cpp
    tests/pipeline/EscRestoreRawFeatureTest.cpp
    tests/pipeline/MacroFeatureTest.cpp
    tests/engine/EngineRuleRegistryTest.cpp
    tests/engine/ToneRuleTest.cpp
    tests/engine/ModifierRuleTest.cpp
    tests/engine/AdjacentCircumflexProposalTest.cpp
    tests/engine/HornWSpeculateParityProbeTest.cpp
    tests/engine/HornModifierProposalTest.cpp
    tests/engine/StrokeDProposalTest.cpp
    tests/engine/BracketProposalTest.cpp
    tests/engine/VniVowelModifierSpeculateParityProbeTest.cpp
    tests/engine/VniProposalsTest.cpp
    tests/engine/QuickStartConsonantRuleTest.cpp
    tests/engine/QuickEndConsonantRuleTest.cpp
    src/app/system/UpdateSecurity.cpp
    src/app/system/MainThreadWorker.cpp
    src/app/system/PerfHistogram.cpp
    src/app/system/HookCommandMailbox.cpp
    src/app/system/HookHijackDetector.cpp
    src/app/system/ReinstallBurstScheduler.cpp
)

# Windows-only tests (require ConfigManager, ConfigEvent, Win32 APIs)
if(WIN32)
    list(APPEND NEXTKEY_TEST_SOURCES
        tests/ConfigManagerTest.cpp
        tests/ConfigEventTest.cpp
        tests/SmartSwitchPersistenceTest.cpp
        tests/HeartbeatPublisherTest.cpp
        tests/core/config/ConfigSnapshotBuilderTest.cpp
        # Sprint 2 T3 — IOutputInjector unit tests + impl sources for linkage.
        # VKeyTests links VKeyCore (which excludes output/), so impls
        # are pulled in directly here for the test binary.
        tests/output/Win32SendInputInjectorTest.cpp
        tests/output/RichEditEmReplaceSelInjectorTest.cpp
        tests/output/SplitDispatchInjectorTest.cpp
        tests/output/OutputInjectorFactoryTest.cpp
        tests/output/InjectorTraitsTest.cpp
        src/app/output/Internal.cpp
        src/app/output/Win32SendInputInjector.cpp
        src/app/output/ClipboardInjector.cpp
        src/app/output/RichEditEmReplaceSelInjector.cpp
        src/app/output/SplitDispatchInjector.cpp
        src/app/output/OutputInjectorFactory.cpp
        src/app/system/HeartbeatPublisher.cpp
    )
endif()

add_executable(VKeyTests EXCLUDE_FROM_ALL ${NEXTKEY_TEST_SOURCES})
target_link_libraries(VKeyTests PRIVATE VKeyCore GTest::gtest_main)
target_include_directories(VKeyTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/tomlplusplus
)
if(MSVC)
    # Test diagnostics narrow wide strings to char for gtest's ostream
    # (pure-ASCII filenames/timestamps, e.g. LoggerTest). Benign C4244 that the
    # MSVC 14.50 toolchain promotes to error via the global /WX — allow it for
    # the test target only; production targets keep /WX fully strict.
    target_compile_options(VKeyTests PRIVATE /wd4244)
endif()

# Put tests in separate directory; override global /SUBSYSTEM:WINDOWS
set_target_properties(VKeyTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
if(MSVC)
    target_link_options(VKeyTests PRIVATE /SUBSYSTEM:CONSOLE)
endif()

include(GoogleTest)
gtest_discover_tests(VKeyTests)

# ============================================================================
# Tools — VKeyTestRunner: cross-platform tests + Windows-only runner exe
# (sub-CMakeLists gates platform-specific targets internally)
#
# Lives in the private `internal/` submodule after the public/private split
# (commit 58960e6). Public CI clones without submodule init → skip silently.
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/internal/tools/VKeyTestRunner/CMakeLists.txt")
    add_subdirectory(internal/tools/VKeyTestRunner)
endif()

# ============================================================================
# Watchdog — standalone Windows-only process supervisor (VKeyWatchdog.exe)
# (sub-CMakeLists return()s on non-WIN32)
# ============================================================================
add_subdirectory(src/watchdog)
