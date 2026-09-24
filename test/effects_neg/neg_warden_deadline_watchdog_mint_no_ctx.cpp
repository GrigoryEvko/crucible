// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_deadline_watchdog takes the context first, and the parameter has
// no default.  A call with no context finds no matching function, so no
// scope builds a watchdog without the evidence of process startup.

#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>

int main() {
    crucible::warden::Policy policy = crucible::warden::Policy::production();
    auto wd = crucible::warden::mint_deadline_watchdog(nullptr, policy);
    (void)wd;
    return 0;
}
