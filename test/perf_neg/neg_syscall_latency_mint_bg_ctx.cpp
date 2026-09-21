// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_syscall_latency rejects BgDrainCtx.  That context claims
// Row<Bg, Alloc> and carries neither IO nor Block, and the gate demands
// all three of Alloc, IO and Block.  SyscallLatency::load() attaches a
// BPF program to raw_syscalls sys_enter and sys_exit and maps the
// per-CPU histogram, and the bpf(BPF_PROG_LOAD) call waits on the
// kernel verifier.

#include <crucible/perf/SyscallLatency.h>

int main() {
    auto hub =
        crucible::perf::mint_syscall_latency(crucible::effects::BgDrainCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
