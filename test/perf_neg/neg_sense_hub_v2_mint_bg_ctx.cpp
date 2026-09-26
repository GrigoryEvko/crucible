// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sense_hub_v2 rejects BgDrainCtx.  That context claims
// Row<Bg, Alloc> and carries neither IO nor Block, and the gate demands
// all three of Alloc, IO and Block.  The load calls bpf(BPF_PROG_LOAD),
// which waits on the kernel verifier.  The second argument is a valid
// startup load context, so the gate is the one reason that the compiler
// rejects the call.

#include <crucible/perf/SenseHubV2.h>
#include <fixy/Ctx.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub_v2(::fixy::BgDrainCtx{::foundation::effects::testing::bg()},
                                                 ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    (void)hub;
    return 0;
}
