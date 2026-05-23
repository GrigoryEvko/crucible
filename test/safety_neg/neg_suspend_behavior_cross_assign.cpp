// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-188 SuspendBehavior, mismatch class #2 of 2:
// CROSS-BEHAVIOR TYPE MISMATCH.
//
// Each suspend behavior pins a DISTINCT NTTP, so a PausesOnSuspend
// (CLOCK_MONOTONIC) witness and a KeepsTicking (CLOCK_BOOTTIME) witness are
// UNRELATED wrapper types with no implicit conversion — a monotonic read
// cannot be silently re-labelled as a boot-time read (they diverge by
// exactly the suspend interval).  This pins the static distinction V-194's
// DeadlineWatchdog depends on: a function demanding a KeepsTicking witness
// statically rejects a PausesOnSuspend one.
//
// Distinct from neg_suspend_behavior_pauses_at_watchdog.cpp (a satisfies<>
// constraint at a call); here the failure is a direct type-level assignment
// between two sibling-behavior wrappers.
//
// Expected diagnostic: conversion from / cannot convert / no viable /
// no match for.

#include <crucible/safety/SuspendBehavior.h>

using namespace crucible::safety;

int main() {
    auto monotonic = mint_suspend_behavior<SuspendBehavior_v::PausesOnSuspend,
                                           unsigned long long>(1000);

    // Should FAIL: a KeepsTicking witness cannot be initialized from a
    // PausesOnSuspend one — distinct behavior wrappers, no conversion.
    SuspendBehavior<SuspendBehavior_v::KeepsTicking, unsigned long long> boot = monotonic;

    return static_cast<int>(boot.peek());
}
