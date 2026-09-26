// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sense_hub_v2 rejects ColdInitCtx.  The load calls
// bpf(BPF_PROG_LOAD), which enters the kernel and waits while the
// verifier walks the program, so the row the gate demands carries Block.
// ColdInitCtx claims Row<Init, Alloc, IO> and no Block.  The startup
// load context claims Block on the same capability, and it is the
// context the load takes.  The second argument is a valid startup load
// context, so the gate is the one reason that the compiler rejects the
// call.

#include <crucible/perf/SenseHubV2.h>
#include <fixy/Ctx.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub_v2(::fixy::ColdInitCtx{::foundation::effects::testing::init()},
                                                 ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    (void)hub;
    return 0;
}
