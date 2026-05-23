// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-188 SuspendBehavior, mismatch class #1 of 2:
// PausesOnSuspend REJECTED AT A KeepsTicking DEADLINE-WATCHDOG GATE.
//
// A deadline watchdog must time against a clock that keeps ticking through
// suspend (`satisfies<KeepsTicking>`); otherwise a long suspend looks like
// "no elapsed time" and the watchdog wrongly reports "no stall" right after
// resume.  A PausesOnSuspend (CLOCK_MONOTONIC) witness does NOT satisfy the
// KeepsTicking floor, so feeding it to the watchdog MUST be a compile error,
// forcing the watchdog onto CLOCK_BOOTTIME — closing the Scenario-6
// 10-minute-suspend bug class.
//
// Distinct from neg_suspend_behavior_cross_assign.cpp (a direct type
// assignment); here the failure is the satisfies<> constraint at a call.
//
// Expected diagnostic: constraints not satisfied / no matching function /
// satisfies / arm_watchdog.

#include <crucible/safety/SuspendBehavior.h>

using namespace crucible::safety;

// A deadline watchdog admits only a clock that survives suspend.
template <typename Witness>
    requires (Witness::template satisfies<SuspendBehavior_v::KeepsTicking>)
[[nodiscard]] unsigned long long arm_watchdog(Witness w) {
    return w.peek();
}

int main() {
    auto monotonic = mint_suspend_behavior<SuspendBehavior_v::PausesOnSuspend,
                                           unsigned long long>(1000);

    // Should FAIL: PausesOnSuspend does not satisfy<KeepsTicking>; the
    // watchdog rejects it at the call site.
    return static_cast<int>(arm_watchdog(monotonic));
}
