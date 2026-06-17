// VKey Watchdog - Process Supervisor
// SPDX-License-Identifier: AGPL-3.0-only
//
// Monitors VKey.exe via Local\VKeyHeartbeat (30s pulse, 90s
// timeout = 3 missed pulses). On crash detect (heartbeat stale +
// graceful flag clear + process not in process list), respawns
// VKey.exe via CreateProcessW.
//
// Launched at-logon by Task Scheduler under user account (NOT SYSTEM)
// to keep AV happy and to be able to launch user-session UI.

#include <Windows.h>
#include <TlHelp32.h>
#include <string>

namespace {

constexpr const wchar_t* HEARTBEAT_EVENT_NAME = L"Local\\VKeyHeartbeat";
constexpr const wchar_t* GRACEFUL_SHUTDOWN_EVENT_NAME = L"Local\\VKeyGracefulShutdown";
constexpr DWORD HEARTBEAT_TIMEOUT_MS = 90'000;  // 3× publisher interval
constexpr DWORD POST_RESPAWN_GRACE_MS = 5'000;  // Wait this long after respawn
constexpr DWORD POST_INIT_GRACE_MS = 30'000;    // Initial grace for VKey to start

// Logging helper — append to %LOCALAPPDATA%\VKey\watchdog.log.
// Best-effort: silent failure if file unavailable.
void LogLine(const wchar_t* fmt, ...) {
    wchar_t path[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;
    wcscat_s(path, MAX_PATH, L"\\VKey");
    CreateDirectoryW(path, nullptr);  // best-effort; ERROR_ALREADY_EXISTS is fine
    wcscat_s(path, MAX_PATH, L"\\watchdog.log");

    HANDLE hFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    SetFilePointer(hFile, 0, nullptr, FILE_END);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[1024];
    int n = swprintf_s(buf, 1024, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    va_list args;
    va_start(args, fmt);
    n += vswprintf_s(buf + n, 1024 - n, fmt, args);
    va_end(args);

    if (n < 1023) { buf[n] = L'\n'; buf[n + 1] = L'\0'; n++; }

    DWORD written;
    WriteFile(hFile, buf, n * sizeof(wchar_t), &written, nullptr);
    CloseHandle(hFile);
}

bool IsProcessAlive(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

std::wstring GetVKeyExePath() {
    // Watchdog and VKey live in the same install dir.
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring path(self);
    auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return {};
    return path.substr(0, pos) + L"\\VKey.exe";
}

bool RespawnVKey() {
    std::wstring exePath = GetVKeyExePath();
    if (exePath.empty()) {
        LogLine(L"RespawnVKey: cannot resolve VKey.exe path");
        return false;
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + exePath + L"\"";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        LogLine(L"RespawnVKey: CreateProcess FAILED err=%lu", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    LogLine(L"RespawnVKey: spawned PID=%lu", pi.dwProcessId);
    return true;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Single-instance guard — both VKey app launch and Task Scheduler
    // at-logon trigger may try to start watchdog. The second one bails
    // silently on ERROR_ALREADY_EXISTS so we never run two supervisors
    // racing each other to respawn on the same heartbeat stale event.
    HANDLE singletonMutex = CreateMutexW(nullptr, FALSE,
                                         L"Local\\VKeyWatchdogSingleton");
    if (!singletonMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (singletonMutex) CloseHandle(singletonMutex);
        return 0;
    }

    LogLine(L"Watchdog starting (pid=%lu)", GetCurrentProcessId());

    // Initial grace — VKey may not be up yet at logon.
    Sleep(POST_INIT_GRACE_MS);

    while (true) {
        // Open events fresh each iteration — handles publisher restart
        // (VKey crashed and respawned) by reattaching to new instance.
        HANDLE heartbeat = OpenEventW(SYNCHRONIZE, FALSE, HEARTBEAT_EVENT_NAME);
        HANDLE graceful = OpenEventW(SYNCHRONIZE, FALSE, GRACEFUL_SHUTDOWN_EVENT_NAME);

        if (!heartbeat || !graceful) {
            // Events not yet created → VKey not running.
            if (heartbeat) CloseHandle(heartbeat);
            if (graceful) CloseHandle(graceful);

            if (!IsProcessAlive(L"VKey.exe")) {
                LogLine(L"Heartbeat events absent + process not running → respawn");
                RespawnVKey();
                Sleep(POST_RESPAWN_GRACE_MS);
            } else {
                Sleep(5000);  // Process exists but events not yet up — be patient
            }
            continue;
        }

        DWORD waitResult = WaitForSingleObject(heartbeat, HEARTBEAT_TIMEOUT_MS);
        CloseHandle(heartbeat);

        if (waitResult == WAIT_OBJECT_0) {
            // Pulse received — alive.
            CloseHandle(graceful);
            continue;
        }

        // Heartbeat stale — check graceful flag.
        DWORD gracefulState = WaitForSingleObject(graceful, 0);
        CloseHandle(graceful);

        if (gracefulState == WAIT_OBJECT_0) {
            LogLine(L"Heartbeat stale + graceful flag set → user quit, watchdog exiting");
            return 0;
        }

        // Stale + no graceful — but is process actually dead?
        if (IsProcessAlive(L"VKey.exe")) {
            LogLine(L"Heartbeat stale but process alive (UI hung?) — NOT respawning");
            Sleep(POST_RESPAWN_GRACE_MS);
            continue;
        }

        LogLine(L"Heartbeat stale + graceful clear + process dead → CRASH detected");
        RespawnVKey();
        Sleep(POST_RESPAWN_GRACE_MS);
    }
}
