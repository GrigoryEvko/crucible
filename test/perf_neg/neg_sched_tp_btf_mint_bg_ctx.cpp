// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sched_tp_btf rejects BgDrainCtx.  That context claims
// Row<Bg, Alloc> and carries neither IO nor Block, and the gate demands
// all three of Alloc, IO and Block.  SchedTpBtf::load() attaches a
// CO-RE BPF program to the sched_switch raw tracepoint and maps the
// per-CPU histogram, and the bpf(BPF_PROG_LOAD) call waits on the
// kernel verifier.

#include <crucible/perf/SchedTpBtf.h>

int main() {
    auto hub = crucible::perf::mint_sched_tp_btf(crucible::effects::BgDrainCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
