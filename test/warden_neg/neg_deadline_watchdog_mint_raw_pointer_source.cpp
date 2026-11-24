// Negative-compile test.  This file must fail to compile.
//
// The source of a watchdog is an Option of a Box that the watchdog owns, so
// a watchdog with no source says none.  A raw pointer is no source, so a call with a null
// pointer finds no matching function.  The check file of the header holds
// the same rule for a pointer to a Senses.

#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>
#include <fixy/Ctx.h>

int main() {
    crucible::warden::Policy policy = crucible::warden::Policy::production();
    auto wd = crucible::warden::mint_deadline_watchdog(::fixy::ColdInitCtx{::foundation::effects::testing::init()},
                                                       /*senses=*/nullptr, policy);
    (void)wd;
    return 0;
}
