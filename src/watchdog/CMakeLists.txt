# VKey Watchdog — process supervisor for crash auto-respawn.
# Built only on Windows.

if(NOT WIN32)
    return()
endif()

add_executable(VKeyWatchdog WIN32
    main.cpp
)

target_compile_definitions(VKeyWatchdog PRIVATE
    UNICODE _UNICODE
)

# No external libs needed — uses only kernel32 (CreateProcessW, CreateMutexW, etc.)
# and shell APIs (Process32FirstW from TlHelp32) auto-linked via Windows.h.

if(MSVC)
    target_link_options(VKeyWatchdog PRIVATE /SUBSYSTEM:WINDOWS,6.01 /RELEASE)
endif()

set_target_properties(VKeyWatchdog PROPERTIES
    OUTPUT_NAME "VKeyWatchdog"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
