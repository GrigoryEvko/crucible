// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_pmu_sample rejects BgDrainCtx.  That context claims
// Row<Bg, Alloc> and carries neither IO nor Block, and the gate demands
// all three of Alloc, IO and Block.  PmuSample::load() opens per-CPU
// perf_event_open descriptors and maps the kernel sample ring, and the
// bpf(BPF_PROG_LOAD) call waits on the kernel verifier.

#include <crucible/perf/PmuSample.h>

int main() {
    auto hub = crucible::perf::mint_pmu_sample(crucible::effects::BgDrainCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
