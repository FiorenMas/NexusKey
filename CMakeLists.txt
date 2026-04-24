cmake_minimum_required(VERSION 3.20)
project(NexusKey VERSION 2.1.24 LANGUAGES CXX)

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
    # Linker security flags: ASLR, DEP, High-Entropy ASLR
    add_link_options(/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
    # PE legitimacy: checksum (reduces AV false positives)
    add_link_options(/RELEASE)
endif()

# Per-target include directories (no global include_directories)

# Engine Library (pure C++17, zero platform dependencies)
set(NEXTKEY_ENGINE_SOURCES
    src/core/config/TypingConfig.h
    src/core/engine/TypingEngine.cpp
    src/core/engine/TypingEngine.h
    src/core/engine/EngineFactory.cpp
    src/core/engine/EngineFactory.h
    src/core/engine/IInputEngine.h
    src/core/engine/EngineHelpers.h
    src/core/engine/EnglishProtection.h
    src/core/engine/VietnameseTables.h
    src/core/engine/SpellChecker.cpp
    src/core/engine/SpellChecker.h
    src/core/engine/CodeTableConverter.cpp
    src/core/engine/CodeTableConverter.h
)

add_library(NextKeyEngine STATIC ${NEXTKEY_ENGINE_SOURCES})
target_include_directories(NextKeyEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Core Library (platform layer: SharedState, Config, Debug)
set(NEXTKEY_CORE_SOURCES
    src/core/Debug.h
    src/core/CrashLog.h
    src/core/CrashLog.cpp
    src/core/Strings.h
    src/core/Strings.cpp
    src/core/ipc/SharedState.h
    src/core/ipc/SharedConstants.h
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
    )
endif()

add_library(NextKeyCore STATIC ${NEXTKEY_CORE_SOURCES})
target_include_directories(NextKeyCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/tomlplusplus
)
target_link_libraries(NextKeyCore PUBLIC NextKeyEngine)

# TSF DLL (TIP)
add_library(NextKeyTSF SHARED
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
    src/tsf/InputScopeChecker.h
    src/tsf/LanguageBarButton.h
    src/tsf/LanguageBarButton.cpp
    src/tsf/DisplayAttribute.h
    src/tsf/ComUtils.h
    src/tsf/Define.h
    src/tsf/resource.h
    src/tsf/NexusKeyTSF.rc
    src/tsf/NexusKeyTSF.def
)
target_link_libraries(NextKeyTSF PRIVATE NextKeyCore)
set_target_properties(NextKeyTSF PROPERTIES 
    LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/src/tsf/NexusKeyTSF.def\""
)

# Core App (EXE) - Win32 GUI application with Sciter UI
add_executable(NextKeyApp WIN32
    src/app/main.cpp
    src/app/resource.h
    src/app/NexusKey.rc
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
    # system/
    src/app/system/TrayIcon.h
    src/app/system/TrayIcon.cpp
    src/app/system/FloatingIcon.h
    src/app/system/FloatingIcon.cpp
    src/app/system/HookEngine.h
    src/app/system/HookEngine.cpp
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
    src/app/system/StartupHelper.h
    src/app/system/SubprocessHelper.h
    src/app/system/SubprocessRunners.h
    src/app/system/SubprocessRunners.cpp
    src/app/system/TsfRegistration.h
    src/app/system/TsfRegistration.cpp
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
    src/app/NexusKey.exe.manifest
)
target_include_directories(NextKeyApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/sciter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/system
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/sciter
)
target_compile_definitions(NextKeyApp PRIVATE SKIP_MAIN NEXUSKEY_HOOK_ENGINE $<$<CONFIG:Debug>:NEXTKEY_DEBUG>)
target_link_libraries(NextKeyApp PRIVATE NextKeyCore)
set_target_properties(NextKeyApp PROPERTIES OUTPUT_NAME "NexusKey")
if(MSVC)
    target_link_options(NextKeyApp PRIVATE /SUBSYSTEM:WINDOWS,6.01)
endif()

# ============================================================================
# Classic UI (Lite build) — Win32 native, no Sciter dependency
# Build with: cmake -B build -DNEXUSKEY_LITE_MODE=ON
# ============================================================================
option(NEXUSKEY_LITE_MODE "Build Classic (Win32 native) UI instead of Sciter" OFF)

if(NEXUSKEY_LITE_MODE AND WIN32)
    add_executable(NextKeyLite WIN32
        src/app/main_lite.cpp
        src/app/classic/resource.h
        src/app/classic/NexusKeyLite.rc
        src/app/classic/NexusKeyLite.exe.manifest
        # system/ (shared with main app)
        src/app/system/TrayIcon.h
        src/app/system/TrayIcon.cpp
        src/app/system/FloatingIcon.h
        src/app/system/FloatingIcon.cpp
        src/app/system/HookEngine.h
        src/app/system/HookEngine.cpp
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
        src/app/system/StartupHelper.h
        src/app/system/SubprocessHelper.h
        src/app/system/TsfRegistration.h
        src/app/system/TsfRegistration.cpp
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
        src/app/classic/ClassicMacroTableDialog.h
        src/app/classic/ClassicMacroTableDialog.cpp
        src/app/classic/ClassicAppOverridesDialog.h
        src/app/classic/ClassicAppOverridesDialog.cpp
        src/app/classic/ClassicConvertToolDialog.h
        src/app/classic/ClassicConvertToolDialog.cpp
        src/app/classic/ClassicIconColorDialog.h
        src/app/classic/ClassicIconColorDialog.cpp
        src/app/classic/ClassicSpellExclusionsDialog.h
        src/app/classic/ClassicSpellExclusionsDialog.cpp
        # version
        src/core/Version.h
    )
    target_include_directories(NextKeyLite PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/classic
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/system
    )
    target_compile_definitions(NextKeyLite PRIVATE
        NEXUSKEY_HOOK_ENGINE
        NEXUSKEY_LITE_MODE
        $<$<CONFIG:Debug>:NEXTKEY_DEBUG>
    )
    target_link_libraries(NextKeyLite PRIVATE
        NextKeyCore
        advapi32 user32 ole32 shell32 dwmapi uuid urlmon version comctl32 winmm bcrypt gdiplus
    )
    set_target_properties(NextKeyLite PROPERTIES OUTPUT_NAME "NexusKeyClassic")
    if(MSVC)
        target_link_options(NextKeyLite PRIVATE /SUBSYSTEM:WINDOWS,6.01)
    endif()
endif()

# Link Windows libraries
if(WIN32)
    target_link_libraries(NextKeyTSF PRIVATE advapi32 user32 ole32 oleaut32 uuid shell32)
    target_link_libraries(NextKeyApp PRIVATE advapi32 user32 ole32 shell32 dwmapi uuid urlmon version comctl32 winmm bcrypt)

    # Copy sciter.dll to output directory
    add_custom_command(TARGET NextKeyApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/extern/sciter/bin/sciter.dll"
            "$<TARGET_FILE_DIR:NextKeyApp>/sciter.dll"
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
    add_dependencies(NextKeyApp PackUIResources)

    # Enable packfolder for Release, file copy for Debug
    target_compile_definitions(NextKeyApp PRIVATE
        $<$<CONFIG:Release>:SCITER_USE_PACKFOLDER>
        $<$<CONFIG:RelWithDebInfo>:SCITER_USE_PACKFOLDER>
    )
    message(STATUS "Sciter: Release=embedded resources, Debug=file copy")
endif()

# Always copy UI files for Debug builds (allows live editing)
add_custom_command(TARGET NextKeyApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Debug>,copy_directory,true>
        "${UI_SOURCE_DIR}"
        "$<TARGET_FILE_DIR:NextKeyApp>/ui"
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

# Tests are excluded from ALL - build explicitly with --target NextKeyTests
set(NEXTKEY_TEST_SOURCES
    tests/TelexEngineTest.cpp
    tests/CombinedEngineTest.cpp
    tests/VniParityTest.cpp
    tests/TelexDictionaryTest.cpp
    tests/EngineFactoryTest.cpp
    tests/SharedStateTest.cpp
    tests/HookContextAnchorTest.cpp
    tests/FeatureOptionsTest.cpp
    tests/SpellCheckerTest.cpp
    tests/CodeTableConverterTest.cpp
    tests/UpdateSecurityTest.cpp
    tests/EngineBenchmarkTest.cpp
    tests/MacroPrefixTest.cpp
    src/app/system/UpdateSecurity.cpp
)

# Windows-only tests (require ConfigManager, ConfigEvent)
if(WIN32)
    list(APPEND NEXTKEY_TEST_SOURCES
        tests/ConfigManagerTest.cpp
        tests/ConfigEventTest.cpp
    )
endif()

add_executable(NextKeyTests EXCLUDE_FROM_ALL ${NEXTKEY_TEST_SOURCES})
target_link_libraries(NextKeyTests PRIVATE NextKeyCore GTest::gtest_main)

# Put tests in separate directory; override global /SUBSYSTEM:WINDOWS
set_target_properties(NextKeyTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
if(MSVC)
    target_link_options(NextKeyTests PRIVATE /SUBSYSTEM:CONSOLE)
endif()

include(GoogleTest)
gtest_discover_tests(NextKeyTests)
