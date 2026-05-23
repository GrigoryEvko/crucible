// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-194 DeadlineWatchdog::observe, mismatch class #1 of 2:
// HOT FG CONTEXT REJECTED.
//
// warden::DeadlineWatchdog::observe is Bg/Init/Test-row-gated via
// CtxFitsDeadlineWatchdog<Ctx> — a hot-foreground replay-bound thread
// (HotFgCtx) owns none of those rows, so the gate rejects.  This
// closes the Scenario-6 invariant: the watchdog is a warden poller,
// not a hot-path query.
//
// Distinct from neg_deadline_watchdog_non_exec_ctx.cpp (ctx-type
// constraint); here the failure is the row-content constraint.
//
// Expected diagnostic: constraints not satisfied / CtxFitsDeadlineWatchdog /
// CtxOwnsAnyOf / no matching function / observe.

#include <crucible/effects/ExecCtx.h>
#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>

int main() {
    using ::crucible::warden::DeadlineWatchdog;
    using ::crucible::warden::Policy;

    DeadlineWatchdog watchdog{
        /*senses=*/nullptr,
        Policy::production(),
        ::crucible::effects::testing::init()};

    ::crucible::effects::HotFgCtx fg{};
    // Should FAIL: HotFgCtx owns neither Bg, Init, nor Test.
    auto v = watchdog.observe(fg);
    (void)v;
    return 0;
}
