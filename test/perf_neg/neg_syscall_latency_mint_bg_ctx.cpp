// FIXY-U-083 HS14 neg-compile fixture (1 of 2 for mint_syscall_latency).
//
// mint_syscall_latency rejects BgDrainCtx — BgDrain carries Bg +
// Alloc but neither engages Init.  SyscallLatency::load() attaches
// a BPF program to raw_syscalls sys_enter/sys_exit and mmaps the
// per-CPU histogram, startup-only work that must remain in the
// Init row.

#include <crucible/perf/SyscallLatency.h>

int main() {
    auto hub = crucible::perf::mint_syscall_latency(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
