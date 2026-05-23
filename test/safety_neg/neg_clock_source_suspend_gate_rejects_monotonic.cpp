// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-185 ClockSource, mismatch class #2 of 2:
// SUSPEND-AXIS REQUIREMENT GATE (the V-194 DeadlineWatchdog gate).
//
// A deadline watchdog requires a clock that keeps ticking through suspend
// — `Clock::satisfies<Boot>` (Boot's KeepsTicking floor).  A
// `MonotonicClockBytes` projects to PausesOnSuspend, so it does NOT
// subsume the Boot requirement and the constraint is unsatisfied.  This
// pins the load-bearing rejection V-194 depends on: feeding a
// CLOCK_MONOTONIC read to the deadline watchdog under-counts wall time
// across a suspend/resume cycle and must be a compile error, not a
// silent latent bug.
//
// Distinct from neg_clock_source_cross_source_assign.cpp, which fails at a
// direct type assignment; here the failure is a CONSTRAINT
// (satisfies<Boot>) not being satisfied at a function call.
//
// Expected diagnostic: constraint not satisfied / no matching function /
// candidate ... constraints not satisfied / satisfies.

#include <crucible/safety/ClockSource.h>

using namespace crucible::safety;

// A deadline watchdog admits only clocks that keep ticking through
// suspend (subsume a Boot requirement).
template <typename Clock>
    requires (Clock::template satisfies<ClockSource_v::Boot>)
[[nodiscard]] unsigned long long arm_deadline(Clock clock) {
    return clock.peek();
}

int main() {
    auto mono = mint_clock_source<ClockSource_v::Monotonic, unsigned long long>(1000);

    // Should FAIL: the mint_clock_source<Monotonic> result pauses on
    // suspend, so it does NOT satisfy<Boot> — the requires-clause rejects
    // it at the call site.
    return static_cast<int>(arm_deadline(mono));
}
