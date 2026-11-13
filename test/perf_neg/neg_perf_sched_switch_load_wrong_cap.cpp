// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// load_sched_switch takes the startup load context.  A background load
// context also claims Block, but its capability source is the background
// source.  No conversion makes it a startup load context, so the compiler
// rejects the call.

#include <crucible/perf/SchedSwitch.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::BgLoadCtx background{::foundation::effects::testing::bg()};
    ::fixy::Option<crucible::perf::SchedSwitch> hub = crucible::perf::load_sched_switch(background);
    (void)hub;
    return 0;
}
