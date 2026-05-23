// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-193 MonotonicClock::now_ns, mismatch class #2 of 2:
// NON-EXEC-CONTEXT FIRST ARGUMENT.
//
// safety::MonotonicClock::now_ns is constrained on
// `effects::IsExecCtx`.  A bare `int` is not an ExecCtx, so the
// constrained template parameter rejects it BEFORE the row-content
// check (CtxFitsMonotonicClock) runs.
//
// Distinct from neg_monotonic_clock_hot_ctx.cpp (row-content
// constraint); here the failure is the ctx-type constraint.
//
// Expected diagnostic: constraints not satisfied / IsExecCtx /
// no matching function / now_ns.

#include <crucible/safety/Mutation.h>

int main() {
    ::crucible::safety::MonotonicClock clock{};

    // Should FAIL: 0 (int) is not an effects::IsExecCtx.
    auto t = clock.now_ns(0);
    (void)t;
    return 0;
}
