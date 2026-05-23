// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-194 DeadlineWatchdog::observe, mismatch class #2 of 2:
// NON-EXEC-CONTEXT FIRST ARGUMENT.
//
// warden::DeadlineWatchdog::observe is constrained on
// `effects::IsExecCtx`.  A bare `int` is not an ExecCtx, so the
// constrained template parameter rejects it BEFORE the row-content
// check (CtxFitsDeadlineWatchdog) runs.
//
// Distinct from neg_deadline_watchdog_hot_ctx.cpp (row-content
// constraint); here the failure is the ctx-type constraint.
//
// Expected diagnostic: constraints not satisfied / IsExecCtx /
// no matching function / observe.

#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>

int main() {
    using ::crucible::warden::DeadlineWatchdog;
    using ::crucible::warden::Policy;

    DeadlineWatchdog watchdog{
        /*senses=*/nullptr,
        Policy::production(),
        ::crucible::effects::testing::init()};

    // Should FAIL: 7 (int) is not an effects::IsExecCtx.
    auto v = watchdog.observe(7);
    (void)v;
    return 0;
}
