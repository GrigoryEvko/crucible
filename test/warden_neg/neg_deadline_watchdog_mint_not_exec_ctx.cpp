// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_deadline_watchdog rejects a type that is not an execution context
// of the new tree.  The first conjunct of the gate, IsExecCtx, fails
// before the gate asks for the Init atom.

#include <crucible/warden/DeadlineWatchdog.h>

struct NotAnExecCtx {};

int main() {
    crucible::warden::Policy p{};
    p.deadline_miss_budget = 100;
    p.watchdog_window_sec = 1;
    auto watchdog = crucible::warden::mint_deadline_watchdog(NotAnExecCtx{}, /*senses=*/nullptr, p);
    (void)watchdog;
    return 0;
}
