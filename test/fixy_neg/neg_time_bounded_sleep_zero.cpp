// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-190 mint_bounded_sleep, mismatch class #2 of 2:
// ZERO STATIC CAP.
//
// A BoundedSleeper<MaxNanos> with MaxNanos == 0 is degenerate — every
// sleep_for would have to be exactly 0.  The mint requires MaxNanos > 0,
// so a zero cap is a compile error.
//
// Distinct from neg_time_bounded_sleep_no_block.cpp (a ctx-capability
// gate); here the failure is the MaxNanos > 0 NTTP constraint.
//
// Expected diagnostic: constraints not satisfied / MaxNanos /
// no matching function / mint_bounded_sleep.

#include <crucible/fixy/Time.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::BgDrainCtx bg{};  // Block-capable, so only the cap fails

    // Should FAIL: MaxNanos == 0 is rejected (degenerate cap).
    auto sleeper = ::crucible::fixy::time::mint_bounded_sleep<0>(bg);
    sleeper.sleep_for(0);
    return 0;
}
