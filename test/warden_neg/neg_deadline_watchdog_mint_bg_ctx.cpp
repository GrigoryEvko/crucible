// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_deadline_watchdog rejects a background context.  Building a
// watchdog takes the baseline of the first window, which belongs to
// process startup, so the gate asks for the Init atom.  The background
// load context claims Bg, Alloc, IO and Block, and not Init.  A
// background thread observes a watchdog that startup built.

#include <crucible/warden/DeadlineWatchdog.h>
#include <fixy/Ctx.h>

int main() {
    crucible::warden::Policy p{};
    p.deadline_miss_budget = 100;
    p.watchdog_window_sec = 1;
    auto watchdog = crucible::warden::mint_deadline_watchdog(::fixy::BgLoadCtx{::foundation::effects::testing::bg()},
                                                             /*senses=*/nullptr, p);
    (void)watchdog;
    return 0;
}
