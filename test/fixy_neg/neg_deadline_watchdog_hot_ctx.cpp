// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// DeadlineWatchdog::observe asks for a context that owns Bg, Init or
// Test (CtxFitsDeadlineWatchdog).  The old-tree HotFgCtx owns none of
// them, so the gate rejects it, and the hot path never polls the
// watchdog.  The watchdog itself comes from the mint with a valid
// startup context, so the observe call is the one reason for the
// refusal.

#include <crucible/effects/_ExecCtx.h>
#include <crucible/warden/DeadlineWatchdog.h>
#include <crucible/warden/Policy.h>
#include <fixy/Ctx.h>

int main() {
    using ::crucible::warden::Policy;

    auto watchdog = ::crucible::warden::mint_deadline_watchdog(::fixy::ColdInitCtx{::foundation::effects::testing::init()},
                                                               /*senses=*/nullptr, Policy::production());

    ::crucible::effects::HotFgCtx fg{};
    auto v = watchdog.observe(fg);
    (void)v;
    return 0;
}
