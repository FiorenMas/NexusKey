// VKey - Self-Update Installer Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "UpdateInstaller.h"
#include "UpdateSecurity.h"
#include "core/Debug.h"

#include <TlHelp32.h>
#include <filesystem>
#include <fstream>
#include <vector>

namespace NextKey {

namespace {

/// Get exe directory
std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0) return L".";
    std::wstring fullPath(path, len);
    auto pos = fullPath.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? fullPath.substr(0, pos) : L".";
}

/// Get full exe path
std::wstring GetExePath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

/// Get current process ID
DWORD GetCurrentPID() {
    return GetCurrentProcessId();
}

/// Wait for all other instances of this app to exit (up to timeoutMs)
bool WaitForOtherProcesses(DWORD timeoutMs) {
    DWORD myPid = GetCurrentPID();
    
    wchar_t myPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, myPath, MAX_PATH);
    std::wstring myName = myPath;
    auto pos = myName.find_last_of(L"\\/");
    if (pos != std::wstring::npos) myName = myName.substr(pos + 1);

    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < timeoutMs) {
        bool othersRunning = false;

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) break;

        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);

        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID != myPid &&
                    (_wcsicmp(pe.szExeFile, myName.c_str()) == 0)) {
                    othersRunning = true;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }

        CloseHandle(snap);

        if (!othersRunning) return true;
        Sleep(500);
    }

    return false;  // Timeout
}

/// Extract ZIP using PowerShell Expand-Archive (hidden window)
bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir) {
    std::wstring safeZipPath = EscapePowerShellSingleQuote(zipPath);
    std::wstring safeDestDir = EscapePowerShellSingleQuote(destDir);
    if (safeZipPath.empty() || safeDestDir.empty()) return false;

    // Build PowerShell command
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"";
    cmd += L"Expand-Archive -Path '";
    cmd += safeZipPath;
    cmd += L"' -DestinationPath '";
    cmd += safeDestDir;
    cmd += L"' -Force\"";

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 60000);  // 60s timeout

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return exitCode == 0;
}

/// Replace a locked-prone DLL. Tries MoveFileW on the live copy first — NTFS
/// allows same-volume rename of image-mapped DLLs because the image section is
/// opened with FILE_SHARE_DELETE. If that succeeds, we copy the new file in
/// place and processes that haven't loaded the DLL yet pick up the new version
/// immediately.
///
/// If rename fails (rare: AV holding a non-share-delete handle), stash the new
/// DLL next to the old one with a `.pending` suffix and drop a marker file so
/// WinMain applies the swap on the next EXE launch.
void HandleTsfDllReplace(const std::wstring& newDllSrc,
                         const std::wstring& exeDir,
                         const std::wstring& oldVersionDir) {
    namespace fs = std::filesystem;

    std::wstring liveDll = exeDir + L"\\" + TSF_DLL_FILENAME;
    std::wstring parked  = oldVersionDir + L"\\" + TSF_DLL_FILENAME
                         + MakeParkedDllTimestamp();

    std::error_code ec;
    fs::create_directories(oldVersionDir, ec);

    if (MoveFileW(liveDll.c_str(), parked.c_str())) {
        if (CopyFileW(newDllSrc.c_str(), liveDll.c_str(), FALSE)) {
            DeleteFileW((exeDir + L"\\" + TSF_DLL_FILENAME + TSF_DLL_PENDING_SUFFIX).c_str());
            DeleteFileW((exeDir + L"\\" + TSF_DLL_PENDING_MARKER).c_str());
            return;
        }
        MoveFileW(parked.c_str(), liveDll.c_str());
    }

    // Fallback: defer to next EXE startup.
    std::wstring pendingPath = exeDir + L"\\" + TSF_DLL_FILENAME + TSF_DLL_PENDING_SUFFIX;
    if (!CopyFileW(newDllSrc.c_str(), pendingPath.c_str(), FALSE)) {
        // Disk full / permission denied / AV quarantine — leave NO marker so
        // the next boot's ApplyPendingDllUpdate doesn't see half-deferred
        // state. User retains the old DLL; EXE may be newer but ABI gate
        // will catch it on DLL-load.
        return;
    }

    // Marker body = SHA256 of the pending DLL. ApplyPendingDllUpdate recomputes
    // and verifies before swapping — prevents applying a user-dropped or
    // corrupted `.pending` file.
    std::string sha = ComputeFileSha256(pendingPath);
    if (sha.empty()) {
        NEXTKEY_LOG(L"HandleTsfDllReplace: SHA-256 of pending DLL failed — discarding");
        DeleteFileW(pendingPath.c_str());
        return;
    }
    std::wstring markerPath = exeDir + L"\\" + TSF_DLL_PENDING_MARKER;
    HANDLE hMarker = CreateFileW(markerPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hMarker == INVALID_HANDLE_VALUE) {
        NEXTKEY_LOG(L"HandleTsfDllReplace: CreateFileW marker failed (err=%lu) — discarding pending",
                    GetLastError());
        DeleteFileW(pendingPath.c_str());  // back out of the half-deferred state
        return;
    }
    DWORD written = 0;
    BOOL wrote = WriteFile(hMarker, sha.data(), static_cast<DWORD>(sha.size()), &written, nullptr);
    CloseHandle(hMarker);
    if (!wrote || written != sha.size()) {
        NEXTKEY_LOG(L"HandleTsfDllReplace: marker WriteFile short/failed (wrote=%lu/%zu err=%lu) — discarding",
                    written, sha.size(), GetLastError());
        DeleteFileW(markerPath.c_str());
        DeleteFileW(pendingPath.c_str());
    }
}

/// Copy all files from srcDir to destDir (overwriting)
bool CopyDirectoryContents(const std::wstring& srcDir, const std::wstring& destDir) {
    try {
        namespace fs = std::filesystem;
        for (const auto& entry : fs::recursive_directory_iterator(srcDir)) {
            auto relativePath = fs::relative(entry.path(), srcDir);

            // Defense-in-depth: reject paths with ".." to prevent directory traversal
            if (relativePath.wstring().find(L"..") != std::wstring::npos) continue;

            auto destPath = fs::path(destDir) / relativePath;

            if (entry.is_directory()) {
                fs::create_directories(destPath);
            } else {
                fs::create_directories(destPath.parent_path());
                // Preserving config.toml if it already exists at the root level:
                if (_wcsicmp(relativePath.wstring().c_str(), L"config.toml") == 0 && fs::exists(destPath)) {
                    continue; // Skip overwriting config.toml
                }
                fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

std::wstring MakeParkedDllTimestamp(const wchar_t* extraSuffix) noexcept {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    // Include PID + tick-count low word to disambiguate same-second collisions
    // (e.g. user double-clicks the installer and two --install-update processes
    // race to park the live DLL).
    wchar_t ts[96];
    swprintf_s(ts, L"_%04u%02u%02u_%02u%02u%02u_%lu_%lx%s",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond,
               GetCurrentProcessId(),
               GetTickCount() & 0xFFFFu,
               extraSuffix ? extraSuffix : L"");
    return ts;
}

[[noreturn]] void RunUpdateInstaller(const std::wstring& zipPath) {
    std::wstring exeDir = GetExeDirectory();
    std::wstring currentExePath = GetExePath();
    std::wstring tempDir = exeDir + L"\\_update_temp";

    // Known-clean start: delete any stale .pending / marker from a prior
    // aborted update. Otherwise a rollback in this run + stale pending from
    // a prior run could pair an old .pending DLL with a freshly-rolled-back
    // EXE and apply mismatched bits at the next boot.
    DeleteFileW((exeDir + L"\\" + TSF_DLL_FILENAME + TSF_DLL_PENDING_SUFFIX).c_str());
    DeleteFileW((exeDir + L"\\" + TSF_DLL_PENDING_MARKER).c_str());

    // 1. Wait for all other VKey.exe processes to exit (120s timeout)
    WaitForOtherProcesses(120000);

    // 2. Move ALL .exe and .dll files to _old_version/ folder
    // This handles sciter.dll, TSF DLLs, and the main EXE regardless of name.
    std::wstring oldVersionDir = exeDir + L"\\_old_version";
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(oldVersionDir, ec);

        for (const auto& entry : fs::directory_iterator(exeDir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().wstring();
            if (_wcsicmp(ext.c_str(), L".exe") == 0 || _wcsicmp(ext.c_str(), L".dll") == 0) {
                std::wstring name = entry.path().filename().wstring();

                // TSF DLL is handled separately after extraction — it may be
                // mapped into foreign host processes and cannot be bulk-moved
                // safely alongside the EXE kill path.
                if (_wcsicmp(name.c_str(), TSF_DLL_FILENAME) == 0) continue;

                std::wstring destPath = oldVersionDir + L"\\" + name;
                DeleteFileW(destPath.c_str());
                MoveFileW(entry.path().c_str(), destPath.c_str());
            }
        }
    }

    // 3. Extract ZIP to _update_temp/
    {
        // Clean up any previous temp dir
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }

    bool extracted = ExtractZip(zipPath, tempDir);
    if (!extracted) {
        // Rollback: restore original files from _old_version/ back to exeDir
        namespace fs = std::filesystem;
        std::error_code rollbackEc;
        std::wstring restoredExePath;
        for (const auto& entry : fs::directory_iterator(oldVersionDir, rollbackEc)) {
            if (!entry.is_regular_file()) continue;
            std::wstring name = entry.path().filename().wstring();
            std::wstring destPath = exeDir + L"\\" + name;
            MoveFileW(entry.path().c_str(), destPath.c_str());
            // Track the main exe for relaunch
            if (_wcsicmp(fs::path(name).extension().c_str(), L".exe") == 0 &&
                name.find(L"VKey") != std::wstring::npos &&
                name.find(L"Update") == std::wstring::npos) {
                restoredExePath = destPath;
            }
        }

        // Write failure marker so relaunched app can show notification
        std::wstring markerPath = exeDir + L"\\_update_failed";
        HANDLE hMarker = CreateFileW(markerPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hMarker != INVALID_HANDLE_VALUE) CloseHandle(hMarker);

        // Relaunch the restored app
        if (!restoredExePath.empty()) {
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            std::wstring cmdLine = L"\"" + restoredExePath + L"\"";
            if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                                CREATE_BREAKAWAY_FROM_JOB, nullptr, exeDir.c_str(), &si, &pi)) {
                // Fallback: launch without breakaway if restricted by job object
                ZeroMemory(&pi, sizeof(pi));
                if (CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                                   0, nullptr, exeDir.c_str(), &si, &pi)) {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
            } else {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }

        ExitProcess(1);
    }

    // 4. Detect ZIP structure: root files or single subdirectory
    std::wstring finalExePath;
    {
        namespace fs = std::filesystem;
        std::wstring sourceDir = tempDir;

        // Check if there's a single subdirectory (common ZIP structure)
        std::vector<fs::directory_entry> entries;
        for (const auto& e : fs::directory_iterator(tempDir)) {
            entries.push_back(e);
        }

        if (entries.size() == 1 && entries[0].is_directory()) {
            sourceDir = entries[0].path().wstring();
        }

        // 5a. Special-case TSF DLL (may be mapped in foreign host processes).
        //     CopyDirectoryContents below would blindly try to overwrite the
        //     live copy and silently fail; instead route via HandleTsfDllReplace
        //     which does the NTFS rename trick + pending-swap fallback.
        {
            std::wstring newTsfDll = sourceDir + L"\\" + TSF_DLL_FILENAME;
            if (fs::exists(newTsfDll)) {
                HandleTsfDllReplace(newTsfDll, exeDir, oldVersionDir);
                // Prevent the generic copy from clobbering our decision.
                std::error_code delEc;
                fs::remove(newTsfDll, delEc);
            }
        }

        // 5b. Copy remaining new files to exe directory.
        CopyDirectoryContents(sourceDir, exeDir);

        // 6. Find the main executable to launch
        // Prefer "VKey.exe", then "VKeyClassic.exe"
        const std::vector<std::wstring> preferredNames = { L"VKey.exe", L"VKeyClassic.exe" };
        for (const auto& name : preferredNames) {
            std::wstring testPath = exeDir + L"\\" + name;
            if (fs::exists(testPath)) {
                finalExePath = testPath;
                break;
            }
        }

        // Fallback: find any EXE that looks like the main app
        if (finalExePath.empty()) {
            for (const auto& entry : fs::directory_iterator(exeDir)) {
                if (!entry.is_regular_file()) continue;
                if (_wcsicmp(entry.path().extension().c_str(), L".exe") == 0) {
                    std::wstring name = entry.path().filename().wstring();
                    if (name.find(L"VKey") != std::wstring::npos) {
                        // Skip updater if it's named VKeyUpdate.exe
                        if (name.find(L"Update") == std::wstring::npos) {
                            finalExePath = entry.path().wstring();
                            break;
                        }
                    }
                }
            }
        }
    }

    // 7. Clean up temp files
    DeleteFileW(zipPath.c_str());
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }

    // 8. Launch new executable
    if (!finalExePath.empty()) {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        
        // Quote the path for CreateProcessW cmdline
        std::wstring cmdLine = L"\"" + finalExePath + L"\"";
        
        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                            CREATE_BREAKAWAY_FROM_JOB, nullptr, exeDir.c_str(), &si, &pi)) {
            // Fallback: if breakaway fails due to restricted job object, retry without it
            ZeroMemory(&pi, sizeof(pi));
            if (CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                               0, nullptr, exeDir.c_str(), &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        } else {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    // 9. Exit updater
    ExitProcess(0);
}

bool CleanupOldUpdateFiles() noexcept {
    bool cleaned = false;
    try {
        std::wstring exeDir = GetExeDirectory();
        namespace fs = std::filesystem;

        // 1. Delete *_old.* files (legacy cleanup)
        for (const auto& entry : fs::directory_iterator(exeDir)) {
            if (!entry.is_regular_file()) continue;
            std::wstring name = entry.path().filename().wstring();

            // Check for _old before extension
            auto stem = entry.path().stem().wstring();
            if (stem.size() >= 4 && stem.substr(stem.size() - 4) == L"_old") {
                std::error_code ec;
                if (fs::remove(entry.path(), ec)) {
                    cleaned = true;
                }
            }
        }

        // 2. Delete _old_version/ directory
        std::wstring oldVersionDir = exeDir + L"\\_old_version";
        if (fs::exists(oldVersionDir)) {
            std::error_code ec;
            std::uintmax_t count = fs::remove_all(oldVersionDir, ec);
            if (!ec && count > 0 && count != static_cast<std::uintmax_t>(-1)) {
                cleaned = true;
            }
        }

        // 3. Delete _update_temp/ directory if it exists
        std::wstring tempDir = exeDir + L"\\_update_temp";
        if (fs::exists(tempDir)) {
            std::error_code ec;
            fs::remove_all(tempDir, ec);
        }
    } catch (...) {
        // Cleanup is best-effort
    }
    return cleaned;
}

}  // namespace NextKey
