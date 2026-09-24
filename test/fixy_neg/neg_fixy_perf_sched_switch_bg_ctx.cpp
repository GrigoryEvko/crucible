// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// crucible::fixy::perf::mint_sched_switch is the re-export of the perf
// mint.  Its gate asks for a context row that contains Alloc, IO and
// Block.  The old-tree BgDrainCtx claims Row<Bg, Alloc>, so the gate
// rejects it.  The second argument is a valid startup load context, so
// the gate is the one reason that the compiler rejects the call.

#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/Perf.h>

int main() {
    auto hub = crucible::fixy::perf::mint_sched_switch(crucible::effects::BgDrainCtx{::crucible::effects::testing::bg()},
                                                       ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    (void)hub;
    return 0;
}
