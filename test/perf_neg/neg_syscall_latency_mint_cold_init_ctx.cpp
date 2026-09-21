// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_syscall_latency rejects ColdInitCtx.  The load calls
// bpf(BPF_PROG_LOAD), which enters the kernel and waits while the
// verifier walks the program, so syscall_latency_required_row carries
// Block.  ColdInitCtx carries Alloc and IO and no Block.  The
// initialization capability permits no Block either, so no widening
// rescues it.

#include <crucible/perf/SyscallLatency.h>

int main() {
    auto hub =
        crucible::perf::mint_syscall_latency(crucible::effects::ColdInitCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
