// FIXY-U-083 HS14 neg-compile fixture (2 of 2 for mint_syscall_tp_btf).
//
// mint_syscall_tp_btf rejects HotFgCtx — hot foreground context must
// never engage the multi-ms CO-RE BPF program load + raw-tracepoint
// attach startup cost.  Distinct mismatch class from BgDrainCtx:
// hot-path constraint vs background-drain Bg+Alloc engagement.

#include <crucible/perf/SyscallTpBtf.h>

int main() {
    auto hub = crucible::perf::mint_syscall_tp_btf(
        crucible::effects::HotFgCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
