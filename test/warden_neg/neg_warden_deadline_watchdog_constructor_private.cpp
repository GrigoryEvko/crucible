// Negative-compile test.  This file must fail to compile.
//
// The constructor of DeadlineWatchdog is private, and
// mint_deadline_watchdog is its one friend.  So no scope builds a
// watchdog without a context that owns the Init atom.

#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>

int main() {
    crucible::warden::Policy policy = crucible::warden::Policy::production();
    crucible::warden::DeadlineWatchdog wd{::fixy::none, policy};
    (void)wd;
    return 0;
}
