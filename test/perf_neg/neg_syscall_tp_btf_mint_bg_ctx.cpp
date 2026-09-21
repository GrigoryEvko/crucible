// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_syscall_tp_btf rejects BgDrainCtx.  That context claims
// Row<Bg, Alloc> and carries neither IO nor Block, and the gate demands
// all three of Alloc, IO and Block.  SyscallTpBtf::load() attaches a
// CO-RE BPF program to the raw_syscalls sys_enter and sys_exit raw
// tracepoints and maps the per-CPU histogram, and the
// bpf(BPF_PROG_LOAD) call waits on the kernel verifier.

#include <crucible/perf/SyscallTpBtf.h>

int main() {
    auto hub = crucible::perf::mint_syscall_tp_btf(crucible::effects::BgDrainCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
