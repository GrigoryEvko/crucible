// FIXY-U-084 HS14 neg-compile fixture (6 of 6).
//
// mint_deadline_watchdog rejects HotFgCtx.  HotFgCtx's row is empty —
// the hot foreground cannot stand up a watchdog (the steady_clock
// baseline read and Senses pointer chase are both cold-path
// operations).  This fixture closes the §XXI bug class where a
// future caller might pass `effects::Init{}` as the cap-tag arg to
// the bare ctor from a hot context, sidestepping row admission.

#include <crucible/warden/DeadlineWatchdog.h>

int main() {
    crucible::warden::Policy p{};
    p.deadline_miss_budget = 100;
    p.watchdog_window_sec = 1;
    auto watchdog = crucible::warden::mint_deadline_watchdog(
        crucible::effects::HotFgCtx{}, /*senses=*/nullptr, p);
    (void)watchdog;
    return 0;
}
