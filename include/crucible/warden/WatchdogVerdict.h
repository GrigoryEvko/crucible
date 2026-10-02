#pragma once

// The verdict of the deadline watchdog (crucible/warden/DeadlineWatchdog.h).
//
// The runtime hub stores and returns a verdict, and it names no member of
// the watchdog, so the verdict has a header of its own.

#include <cstdint>

namespace crucible::warden {

// InsufficientData carries no information. A caller must not read it
// as "healthy" and must not act on it.
enum class WatchdogVerdict : uint8_t {
    InsufficientData = 0,  // nothing to observe, or the window has not closed
    Healthy = 1,  // the miss count for the closed window is within budget
    Downgrade = 2,  // the budget is exceeded; a weaker class is advised
};

[[nodiscard, gnu::const]] inline const char* watchdog_verdict_name(WatchdogVerdict v) noexcept {
    switch (v) {
        case WatchdogVerdict::InsufficientData:
            return "InsufficientData";
        case WatchdogVerdict::Healthy:
            return "Healthy";
        case WatchdogVerdict::Downgrade:
            return "Downgrade";
        default:
            return "Invalid";
    }
}

}  // namespace crucible::warden
