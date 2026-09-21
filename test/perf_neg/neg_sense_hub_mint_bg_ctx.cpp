// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sense_hub rejects BgDrainCtx.  That context claims
// Row<Bg, Alloc> and carries neither IO nor Block, and the gate demands
// all three of Alloc, IO and Block.  SenseHub::load() loads a BPF
// program and attaches a tracepoint through bpf() and
// perf_event_open(), and maps the counter array.  The
// bpf(BPF_PROG_LOAD) call waits on the kernel verifier.

#include <crucible/perf/SenseHub.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub(crucible::effects::BgDrainCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
