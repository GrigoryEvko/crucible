// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-185 ClockSource, mismatch class #1 of 2:
// CROSS-SOURCE TYPE MISMATCH.
//
// Each clock source pins a DISTINCT NTTP, so `BootClockBytes<T>` and
// `MonotonicClockBytes<T>` are UNRELATED wrapper types with no implicit
// conversion between them — a CLOCK_BOOTTIME read cannot be silently
// re-labelled as a CLOCK_MONOTONIC read (they tick differently across
// suspend).  This pins the load-bearing property V-194's DeadlineWatchdog
// depends on: a function demanding `BootClockBytes` statically rejects a
// `MonotonicClockBytes`, and vice versa.
//
// Distinct from neg_clock_source_suspend_gate_rejects_monotonic.cpp, which
// fails at a CONSTRAINT (satisfies<Boot>); here the failure is a direct
// type-level assignment between two sibling source wrappers.
//
// Expected diagnostic: conversion from / cannot convert / no viable / no
// match for.

#include <crucible/safety/ClockSource.h>

using namespace crucible::safety;

int main() {
    auto boot = mint_clock_source<ClockSource_v::Boot, unsigned long long>(42);

    // Should FAIL: a MonotonicClockBytes cannot be initialized from a
    // BootClockBytes (the mint_clock_source<Boot> result) — distinct source
    // wrappers, no conversion.
    MonotonicClockBytes<unsigned long long> mono = boot;

    return static_cast<int>(mono.peek());
}
