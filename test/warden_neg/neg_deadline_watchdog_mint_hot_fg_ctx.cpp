// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_deadline_watchdog rejects the hot foreground context.  Its row is
// empty, so it does not own the Init atom that the gate asks for.  The
// hot path observes no watchdog and builds none.

#include <crucible/warden/DeadlineWatchdog.h>
#include <fixy/Ctx.h>

int main() {
    crucible::warden::Policy p{};
    p.deadline_miss_budget = 100;
    p.watchdog_window_sec = 1;
    const ::fixy::HotFgCtx foreground = ::foundation::effects::testing::foreground();
    auto watchdog = crucible::warden::mint_deadline_watchdog(foreground, /*senses=*/nullptr, p);
    (void)watchdog;
    return 0;
}
