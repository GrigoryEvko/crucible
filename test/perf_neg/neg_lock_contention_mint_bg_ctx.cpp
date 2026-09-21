// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_lock_contention rejects BgDrainCtx.  That context claims
// Row<Bg, Alloc> and carries neither IO nor Block, and the gate demands
// all three of Alloc, IO and Block.  LockContention::load() opens a BPF
// program and a tracepoint and maps the per-CPU histogram, and the
// bpf(BPF_PROG_LOAD) call waits on the kernel verifier.

#include <crucible/perf/LockContention.h>

int main() {
    auto hub =
        crucible::perf::mint_lock_contention(crucible::effects::BgDrainCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
