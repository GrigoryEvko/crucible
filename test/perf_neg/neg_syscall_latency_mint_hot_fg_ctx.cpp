// FIXY-U-083 HS14 neg-compile fixture (2 of 2 for mint_syscall_latency).
//
// mint_syscall_latency rejects HotFgCtx — hot foreground context must
// never engage the multi-ms BPF program load + tracepoint attach
// startup cost.  Distinct mismatch class from BgDrainCtx: hot-path
// constraint vs background-drain Bg+Alloc engagement.

#include <crucible/perf/SyscallLatency.h>

int main() {
    auto hub = crucible::perf::mint_syscall_latency(
        crucible::effects::HotFgCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
