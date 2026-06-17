// VKey - Update Security Helpers Implementation
// SPDX-License-Identifier: AGPL-3.0-only

#include "UpdateSecurity.h"
#include "CancelableBindStatusCallback.h"

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#include <urlmon.h>
#include <fstream>
#include <sstream>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "urlmon.lib")
#endif

namespace NextKey {

// ── SEC-002: PowerShell single-quote escaping ──────────────────────────────

std::wstring EscapePowerShellSingleQuote(const std::wstring& input) noexcept {
    try {
        std::wstring result;
        result.reserve(input.size() + 8);
        for (wchar_t ch : input) {
            result += ch;
            if (ch == L'\'') {
                result += L'\'';  // Double the single quote: ' -> ''
            }
        }
        return result;
    } catch (...) {
        return {};  // Allocation failure — return empty rather than unescaped
    }
}

// ── SEC-003: URL domain validation ─────────────────────────────────────────

namespace {

/// ASCII-only case-insensitive prefix check (URLs are always ASCII in scheme+host).
bool StartsWithIgnoreCase(const std::wstring& str, const wchar_t* prefix) noexcept {
    for (size_t i = 0; prefix[i] != L'\0'; ++i) {
        if (i >= str.size()) return false;
        wchar_t sc = str[i];
        wchar_t pc = prefix[i];
        // ASCII A-Z fold only (safe for URL scheme+host)
        if (sc >= L'A' && sc <= L'Z') sc = sc - L'A' + L'a';
        if (pc >= L'A' && pc <= L'Z') pc = pc - L'A' + L'a';
        if (sc != pc) return false;
    }
    return true;
}

bool StartsWithIgnoreCase(const std::string& str, const char* prefix) noexcept {
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (i >= str.size()) return false;
        char sc = str[i];
        char pc = prefix[i];
        if (sc >= 'A' && sc <= 'Z') sc = sc - 'A' + 'a';
        if (pc >= 'A' && pc <= 'Z') pc = pc - 'A' + 'a';
        if (sc != pc) return false;
    }
    return true;
}

}  // namespace

bool IsAllowedDownloadUrl(const std::wstring& url) noexcept {
    static constexpr const wchar_t* allowedPrefixes[] = {
        L"https://github.com/",
        L"https://objects.githubusercontent.com/",
        L"https://codeload.github.com/",
    };
    for (const auto* prefix : allowedPrefixes) {
        if (StartsWithIgnoreCase(url, prefix)) return true;
    }
    return false;
}

bool IsAllowedDownloadUrl(const std::string& url) noexcept {
    static constexpr const char* allowedPrefixes[] = {
        "https://github.com/",
        "https://objects.githubusercontent.com/",
        "https://codeload.github.com/",
    };
    for (const auto* prefix : allowedPrefixes) {
        if (StartsWithIgnoreCase(url, prefix)) return true;
    }
    return false;
}

// ── SEC-001: ParseSha256File (cross-platform — pure string parsing) ──────────

std::string ParseSha256File(const std::string& content) noexcept {
    try {
        if (content.empty()) return {};

        std::string line = content;

        // Strip trailing whitespace / newlines
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }

        // Extract first token (the hash) — split on space
        std::string hash;
        auto spacePos = line.find(' ');
        if (spacePos != std::string::npos) {
            hash = line.substr(0, spacePos);
        } else {
            hash = line;
        }

        // SHA-256 hash must be exactly 64 hex characters
        if (hash.size() != 64) return {};

        // Validate all chars are hex and lowercase the result
        std::string result;
        result.reserve(64);
        for (char ch : hash) {
            if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')) {
                result += ch;
            } else if (ch >= 'A' && ch <= 'F') {
                result += static_cast<char>(ch + ('a' - 'A'));
            } else {
                return {};  // Not a valid hex character
            }
        }

        return result;
    } catch (...) {
        return {};
    }
}

// ── SEC-001: Windows CNG — SHA-256 compute + ZIP verify ──────────────────────

#ifdef _WIN32

std::string ComputeFileSha256(const std::wstring& filePath) noexcept {
    try {
        struct AlgHandle {
            BCRYPT_ALG_HANDLE h = nullptr;
            ~AlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
        };
        struct HashHandle {
            BCRYPT_HASH_HANDLE h = nullptr;
            ~HashHandle() { if (h) BCryptDestroyHash(h); }
        };
        struct FileHandle {
            HANDLE h = INVALID_HANDLE_VALUE;
            ~FileHandle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
        };

        AlgHandle alg;
        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &alg.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status) || !alg.h) return {};

        DWORD hashObjectSize = 0, cbData = 0;
        status = BCryptGetProperty(alg.h, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PBYTE>(&hashObjectSize), sizeof(DWORD), &cbData, 0);
        if (!BCRYPT_SUCCESS(status)) return {};

        DWORD hashSize = 0;
        status = BCryptGetProperty(alg.h, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PBYTE>(&hashSize), sizeof(DWORD), &cbData, 0);
        if (!BCRYPT_SUCCESS(status) || hashSize != 32) return {};

        std::vector<BYTE> hashObjectBuf(hashObjectSize);

        HashHandle hash;
        status = BCryptCreateHash(alg.h, &hash.h, hashObjectBuf.data(),
            hashObjectSize, nullptr, 0, 0);
        if (!BCRYPT_SUCCESS(status) || !hash.h) return {};

        FileHandle file;
        file.h = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.h == INVALID_HANDLE_VALUE) return {};

        constexpr DWORD kBufSize = 65536;
        std::vector<BYTE> readBuf(kBufSize);
        DWORD bytesRead = 0;

        while (ReadFile(file.h, readBuf.data(), kBufSize, &bytesRead, nullptr) && bytesRead > 0) {
            status = BCryptHashData(hash.h, readBuf.data(), bytesRead, 0);
            if (!BCRYPT_SUCCESS(status)) return {};
        }

        std::vector<BYTE> hashValue(hashSize);
        status = BCryptFinishHash(hash.h, hashValue.data(), hashSize, 0);
        if (!BCRYPT_SUCCESS(status)) return {};

        std::string hexStr;
        hexStr.reserve(64);
        static constexpr char hexChars[] = "0123456789abcdef";
        for (DWORD i = 0; i < hashSize; ++i) {
            hexStr += hexChars[(hashValue[i] >> 4) & 0x0F];
            hexStr += hexChars[hashValue[i] & 0x0F];
        }

        return hexStr;
    } catch (...) {
        return {};
    }
}

// CancelableBindStatusCallback lives in CancelableBindStatusCallback.h
// (shared with UpdateChecker.cpp).

bool VerifyDownloadedZip(
    const std::wstring& zipUrl,
    const std::wstring& localZipPath,
    std::atomic<bool>& cancelFlag) noexcept
{
    try {
        if (cancelFlag.load(std::memory_order_relaxed)) return false;

        // Defense-in-depth: reject non-GitHub URLs even if caller forgot to validate
        if (!IsAllowedDownloadUrl(zipUrl)) return false;

        // 1. Build checksum URL: append ".sha256" to the ZIP URL
        std::wstring checksumUrl = zipUrl + L".sha256";

        // 2. Download checksum file to temp location
        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring checksumPath = std::wstring(tempDir) + L"vkey_checksum.sha256";

        CancelableBindStatusCallback callback(cancelFlag);
        HRESULT hr = URLDownloadToFileW(nullptr, checksumUrl.c_str(),
            checksumPath.c_str(), 0, &callback);
        if (FAILED(hr)) {
            DeleteFileW(checksumPath.c_str());
            return false;
        }

        // 3. Read checksum file content
        std::ifstream checksumFile(checksumPath, std::ios::binary);
        if (!checksumFile.is_open()) {
            DeleteFileW(checksumPath.c_str());
            return false;
        }
        std::ostringstream ss;
        ss << checksumFile.rdbuf();
        checksumFile.close();
        DeleteFileW(checksumPath.c_str());

        std::string checksumContent = ss.str();
        if (checksumContent.empty()) return false;

        // 4. Parse expected hash
        std::string expectedHash = ParseSha256File(checksumContent);
        if (expectedHash.empty()) return false;

        // 5. Compute actual hash of downloaded ZIP
        std::string actualHash = ComputeFileSha256(localZipPath);
        if (actualHash.empty()) return false;

        // 6. Compare (both are lowercase)
        return expectedHash == actualHash;
    } catch (...) {
        return false;
    }
}

#endif  // _WIN32

}  // namespace NextKey
