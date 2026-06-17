// src/app/output/Internal.cpp
//
// Definitions for the test seams declared in Internal.h. Pointers init
// to real Win32 APIs at static-init time.
#include "Internal.h"

namespace NextKey::Output::Internal {

SendInputFn           g_sendInput           = ::SendInput;
SendMessageWFn        g_sendMessageW        = ::SendMessageW;
SendMessageTimeoutWFn g_sendMessageTimeoutW = ::SendMessageTimeoutW;
SleepFn               g_sleep               = ::Sleep;
SynthCounterFn        g_synthCounterCallback = nullptr;

bool TrackedSendInput(INPUT* events, UINT count) noexcept {
    // Pre-increment the synth counter BEFORE SendInput. The Win32
    // callback fires synchronously during SendInput on the same thread —
    // if we incremented after, the LL hook proc could decrement the
    // counter for our just-dispatched events before we'd added them,
    // leaving synthEventsPending_ negative-leaning until watchdog reset.
    if (g_synthCounterCallback && count > 0) {
        g_synthCounterCallback(static_cast<int>(count));
    }
    UINT sent = g_sendInput(count, events, sizeof(INPUT));
    if (sent < count && g_synthCounterCallback) {
        // Compensate: the (count - sent) events never reached the OS
        // queue, so they won't round-trip through the hook to balance
        // the pre-increment. Subtract them out to keep the counter true.
        g_synthCounterCallback(-static_cast<int>(count - sent));
    }
    return sent == count;
}

}  // namespace NextKey::Output::Internal
