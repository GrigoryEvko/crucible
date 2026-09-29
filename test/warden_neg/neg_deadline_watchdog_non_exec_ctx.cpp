// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// DeadlineWatchdog::observe is constrained on IsExecCtx.  A bare int is
// not an execution context, so the constrained template parameter
// rejects it before the row check (CtxFitsDeadlineWatchdog) runs.  The
// watchdog itself comes from the mint with a valid startup context, so
// the observe call is the one reason for the refusal.

#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>
#include <fixy/Ctx.h>

int main() {
    using ::crucible::warden::Policy;

    auto watchdog =
        ::crucible::warden::mint_deadline_watchdog(::fixy::ColdInitCtx{::foundation::effects::testing::init()},
                                                   /*senses=*/nullptr, Policy::production());

    auto v = watchdog.observe(7);
    (void)v;
    return 0;
}
