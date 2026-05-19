// FIXY-U-084 HS14 neg-compile fixture (5 of 6).
//
// mint_deadline_watchdog rejects BgDrainCtx.  Standing up a fresh
// watchdog requires reading Senses + Policy + steady_clock to
// baseline the rolling window — an Init-row act.  The Bg drain
// thread that polls observe() each tick reads from an EXISTING
// watchdog minted at Init time; it does not mint its own.

#include <crucible/warden/DeadlineWatchdog.h>

int main() {
    crucible::warden::Policy p{};
    p.deadline_miss_budget = 100;
    p.watchdog_window_sec = 1;
    auto watchdog = crucible::warden::mint_deadline_watchdog(
        crucible::effects::BgDrainCtx{}, /*senses=*/nullptr, p);
    (void)watchdog;
    return 0;
}
