// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of DeadlineWatchdog is private, and
// mint_deadline_watchdog is its one friend.  So no scope builds a
// watchdog without a context that owns the Init atom.

#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>

int main() {
    crucible::warden::Policy policy = crucible::warden::Policy::production();
    crucible::warden::DeadlineWatchdog wd{nullptr, policy};
    (void)wd;
    return 0;
}
