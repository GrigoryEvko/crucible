// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-193 MonotonicClock::now_ns, mismatch class #1 of 2:
// HOT FG CONTEXT REJECTED.
//
// safety::MonotonicClock::now_ns is Bg/Init/Test-row-gated via
// CtxFitsMonotonicClock<Ctx> — a hot-foreground replay-bound thread
// (HotFgCtx) owns NONE of those rows, so the gate rejects.  This
// closes the Scenario-1 invariant: bit-exact replay cannot tolerate
// the foreground silently sampling the wall clock and observing a
// host-dependent value.
//
// Distinct from neg_monotonic_clock_non_exec_ctx.cpp (ctx-type
// constraint); here the failure is the row-content constraint.
//
// Expected diagnostic: constraints not satisfied / CtxFitsMonotonicClock /
// CtxOwnsAnyOf / no matching function / now_ns.

#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Mutation.h>

int main() {
    ::crucible::safety::MonotonicClock clock{};
    ::crucible::effects::HotFgCtx fg{};

    // Should FAIL: HotFgCtx owns neither Bg, Init, nor Test.
    auto t = clock.now_ns(fg);
    (void)t;
    return 0;
}
