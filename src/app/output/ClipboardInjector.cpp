// src/app/output/ClipboardInjector.cpp
#include "ClipboardInjector.h"
#include "Internal.h"
#include <atomic>
#include <vector>
#include <string>

namespace NextKey::Output {

// Single-instance assumption: HookEngine constructs one ClipboardInjector
// per focus change (replacing the previous injector_ shared_ptr). The save
// → write → restore lifecycle is bounded by the SetTimer 200ms callback
// (RestoreTimerProc), so file-scope state is safe; if multi-instance is
// ever needed (e.g., parallel hook threads), promote to class members.
//
// Only CF_UNICODETEXT is preserved across the restore cycle — images and
// other formats on the clipboard are lost after the first injection.
// Acceptable trade-off for opt-in clipboard-based injection.
//
// SECURITY: Clipboard listeners (apps using `SetClipboardViewer` /
// `AddClipboardFormatListener`) WILL see our injected text — the
// `ExcludeClipboardContentFromMonitor` flag only blocks Windows Clipboard
// History, not third-party listeners. Unavoidable when using the clipboard
// for IPC; same exposure surface as if the user had typed the text via
// clipboard paste themselves.
namespace {
std::wstring       g_originalClipText;
bool               g_hasOriginalClip = false;
UINT_PTR           g_restoreTimerId  = 0;
// Sequence number captured right after our last clipboard write. If
// GetClipboardSequenceNumber() drifts from this value, someone else
// (typically the user pressing Ctrl+C) has touched the clipboard since —
// we must NOT clobber their copy on restore.
std::atomic<DWORD> g_lastWrittenSeq{0};
}  // namespace

// Best-effort restore of g_originalClipText. Skips silently if a third party
// (user Ctrl+C, clipboard manager) touched the clipboard since our last write,
// or if we can't acquire the clipboard lock. Caller is responsible for
// clearing g_originalClipText / g_hasOriginalClip afterward — this helper
// does not, so it composes cleanly with both the timer-driven and the inline
// fallback restore sites.
static void RestoreOriginalClipboardIfSeqMatches(DWORD ourSeq) noexcept {
    if (GetClipboardSequenceNumber() != ourSeq) return;
    if (!OpenClipboard(nullptr)) return;
    // TOCTOU: someone may have raced us between the pre-check above and the
    // lock acquisition. Re-check inside the lock before EmptyClipboard wipes
    // anything.
    if (GetClipboardSequenceNumber() != ourSeq) {
        CloseClipboard();
        return;
    }
    EmptyClipboard();
    if (!g_originalClipText.empty()) {
        size_t byteCount = (g_originalClipText.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteCount);
        if (hMem) {
            void* p = GlobalLock(hMem);
            memcpy(p, g_originalClipText.data(), byteCount);
            GlobalUnlock(hMem);
            // SetClipboardData transfers HGLOBAL ownership on success;
            // on failure the caller still owns it (Win32 docs).
            if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                GlobalFree(hMem);
            }
        }
    }
    CloseClipboard();
}

static VOID CALLBACK RestoreTimerProc(HWND, UINT, UINT_PTR idEvent, DWORD) {
    if (idEvent != g_restoreTimerId) return;
    KillTimer(nullptr, g_restoreTimerId);
    g_restoreTimerId = 0;

    if (!g_hasOriginalClip) return;

    RestoreOriginalClipboardIfSeqMatches(g_lastWrittenSeq.load(std::memory_order_relaxed));

    g_originalClipText.clear();
    g_hasOriginalClip = false;
}

bool ClipboardInjector::Replace(std::size_t bsCount, std::wstring_view text) noexcept {
    if (bsCount == 0 && text.empty()) return true;

    // Fast path: backspace-only — no clipboard IPC needed
    if (text.empty()) {
        std::vector<INPUT> inputs;
        inputs.reserve(bsCount * 2);
        for (std::size_t i = 0; i < bsCount; ++i) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = VK_BACK;
            in.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
            in.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
            inputs.push_back(in);
            in.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(in);
        }
        return Internal::TrackedSendInput(inputs.data(), static_cast<UINT>(inputs.size()));
    }

    // ── Full clipboard injection path ──

    // Cancel pending restore timer (we'll set a new one after this paste)
    if (g_restoreTimerId) {
        KillTimer(nullptr, g_restoreTimerId);
        g_restoreTimerId = 0;
    }

    // 1. Save original clipboard text — first injection per word, OR refresh
    // when the user has copied something between our previous injection and
    // this one (mid-word Ctrl+C). Detected via sequence-number drift.
    const bool clipboardTouchedSinceOurWrite =
        GetClipboardSequenceNumber() != g_lastWrittenSeq.load(std::memory_order_relaxed);
    if (!g_hasOriginalClip || clipboardTouchedSinceOurWrite) {
        g_originalClipText.clear();
        if (OpenClipboard(nullptr)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                const wchar_t* pText = static_cast<const wchar_t*>(GlobalLock(hData));
                if (pText) {
                    g_originalClipText = pText;
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }
        g_hasOriginalClip = true;
    }

    // 2. Write our text to the clipboard.
    //
    // PERF NOTE (Rule 11.3): OpenClipboard is a kernel call that may block
    // briefly if another process holds the clipboard. This runs on the LL
    // hook callback path; combined with the 15ms settle Sleep below it
    // approaches but stays under Windows' default 300ms LowLevelHooksTimeout.
    // Acceptable for opt-in per-app injection; if a host repeatedly trips
    // the timeout, surface as a watchdog log + advise switching the app
    // override back to SendInput.
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();

        size_t byteCount = (text.length() + 1) * sizeof(wchar_t);
        HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE, byteCount);
        if (hText) {
            void* p = GlobalLock(hText);
            memcpy(p, text.data(), text.length() * sizeof(wchar_t));
            static_cast<wchar_t*>(p)[text.length()] = L'\0';
            GlobalUnlock(hText);
            if (!SetClipboardData(CF_UNICODETEXT, hText)) {
                GlobalFree(hText);  // ownership stays with us on failure
            }
        }

        // Exclude from Windows Clipboard History (does not affect 3rd-party
        // clipboard listeners — see file-top SECURITY note).
        static UINT excludeFormat = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitor");
        if (excludeFormat) {
            HGLOBAL hEmpty = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1);
            if (hEmpty) {
                if (!SetClipboardData(excludeFormat, hEmpty)) {
                    GlobalFree(hEmpty);
                }
            }
        }

        CloseClipboard();

        // Snapshot sequence so we can detect any later third-party clipboard
        // mutation (user Ctrl+C, clipboard manager, etc.) before restoring.
        g_lastWrittenSeq.store(GetClipboardSequenceNumber(), std::memory_order_relaxed);
    }

    // 3. Build input sequence: Backspaces + Ctrl+V
    std::vector<INPUT> inputs;
    inputs.reserve(bsCount * 2 + 4);

    for (std::size_t i = 0; i < bsCount; ++i) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = VK_BACK;
        in.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
        in.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
        inputs.push_back(in);
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(in);
    }

    INPUT ctrlDown{};
    ctrlDown.type = INPUT_KEYBOARD;
    ctrlDown.ki.wVk = VK_CONTROL;
    ctrlDown.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC));
    ctrlDown.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
    inputs.push_back(ctrlDown);

    INPUT vDown{};
    vDown.type = INPUT_KEYBOARD;
    vDown.ki.wVk = 'V';
    vDown.ki.wScan = static_cast<WORD>(::MapVirtualKeyW('V', MAPVK_VK_TO_VSC));
    vDown.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
    inputs.push_back(vDown);

    INPUT vUp{};
    vUp.type = INPUT_KEYBOARD;
    vUp.ki.wVk = 'V';
    vUp.ki.wScan = static_cast<WORD>(::MapVirtualKeyW('V', MAPVK_VK_TO_VSC));
    vUp.ki.dwFlags = KEYEVENTF_KEYUP;
    vUp.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
    inputs.push_back(vUp);

    INPUT ctrlUp{};
    ctrlUp.type = INPUT_KEYBOARD;
    ctrlUp.ki.wVk = VK_CONTROL;
    ctrlUp.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC));
    ctrlUp.ki.dwFlags = KEYEVENTF_KEYUP;
    ctrlUp.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
    inputs.push_back(ctrlUp);

    bool result = Internal::TrackedSendInput(inputs.data(), static_cast<UINT>(inputs.size()));

    // 4. Brief settle so the target app processes Ctrl+V before the next
    // keystroke arrives. Load-bearing — without this the next physical key
    // can race the paste and split the word. 15ms keeps the LL hook callback
    // under the default LowLevelHooksTimeout of 300ms (combined with the
    // OpenClipboard call above ~30-50ms worst-case).
    Sleep(15);

    // 5. Schedule lazy restore — fires 200ms after the LAST injection.
    g_restoreTimerId = SetTimer(nullptr, 0, 200, RestoreTimerProc);
    if (g_restoreTimerId == 0 && g_hasOriginalClip) {
        // Timer failed — fall back to inline restore. Helper skips silently
        // on seq drift or lock-acquire failure; either way we drop the cache
        // so the user keeps whatever they last copied.
        RestoreOriginalClipboardIfSeqMatches(g_lastWrittenSeq.load(std::memory_order_relaxed));
        g_originalClipText.clear();
        g_hasOriginalClip = false;
    }

    return result;
}

void ClipboardInjector::SendKey(unsigned short vkCode) noexcept {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vkCode;
    in[0].ki.wScan = static_cast<WORD>(::MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC));
    in[0].ki.dwExtraInfo = Internal::kVKeyExtraInfo;

    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vkCode;
    in[1].ki.wScan = static_cast<WORD>(::MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC));
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    in[1].ki.dwExtraInfo = Internal::kVKeyExtraInfo;

    (void)Internal::TrackedSendInput(in, 2);
}

} // namespace NextKey::Output
