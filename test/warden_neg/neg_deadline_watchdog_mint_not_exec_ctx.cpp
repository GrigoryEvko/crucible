// FIXY-U-084 HS14 strengthening fixture (9 of 9).
//
// Demonstrates the SECOND mismatch class for mint_deadline_watchdog:
// IsExecCtx<Ctx> failing for a raw type.  Companion to fixtures 5-6
// (Bg, HotFg) which exercise the row-membership half of the
// CtxFitsDeadlineWatchdogMint concept.

#include <crucible/warden/DeadlineWatchdog.h>

struct NotAnExecCtx {};

int main() {
    crucible::warden::Policy p{};
    p.deadline_miss_budget = 100;
    p.watchdog_window_sec = 1;
    auto watchdog = crucible::warden::mint_deadline_watchdog(
        NotAnExecCtx{}, /*senses=*/nullptr, p);
    (void)watchdog;
    return 0;
}
