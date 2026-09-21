// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sched_switch rejects BgDrainCtx.  That context claims
// Row<Bg, Alloc> and carries neither IO nor Block, and the gate demands
// all three of Alloc, IO and Block.  SchedSwitch::load() attaches a BPF
// program to the sched_switch tracepoint and maps the per-CPU
// histogram, and the bpf(BPF_PROG_LOAD) call waits on the kernel
// verifier.

#include <crucible/perf/SchedSwitch.h>

int main() {
    auto hub = crucible::perf::mint_sched_switch(crucible::effects::BgDrainCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
