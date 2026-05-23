// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-190 mint_clock_reader, mismatch class #1 of 2:
// NON-CLOCK-BACKED SOURCE.
//
// TscRaw is NOT a clock_gettime-backed source — it is read via rdtsc
// through mint_tsc_reader.  ClockBacked<TscRaw> is false, so the
// clock-reader mint must reject it (forcing the caller to the TSC path).
//
// Distinct from neg_time_clock_reader_non_exec_ctx.cpp (a ctx mismatch);
// here the failure is the source-axis ClockBacked constraint.
//
// Expected diagnostic: constraints not satisfied / ClockBacked /
// no matching function / mint_clock_reader.

#include <crucible/fixy/Time.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::ColdInitCtx init{};

    // Should FAIL: TscRaw is not clock_gettime-backed.
    auto reader = ::crucible::fixy::time::mint_clock_reader<
        ::crucible::fixy::time::ClockSource_v::TscRaw>(init);

    return static_cast<int>(reader.read().peek());
}
