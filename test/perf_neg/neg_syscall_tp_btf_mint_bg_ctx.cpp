// FIXY-U-083 HS14 neg-compile fixture (1 of 2 for mint_syscall_tp_btf).
//
// mint_syscall_tp_btf rejects BgDrainCtx — BgDrain carries Bg + Alloc
// but neither engages Init.  SyscallTpBtf::load() attaches a CO-RE
// BPF program to raw_syscalls sys_enter/sys_exit raw tracepoints and
// mmaps the per-CPU histogram, startup-only work that must remain in
// the Init row.

#include <crucible/perf/SyscallTpBtf.h>

int main() {
    auto hub = crucible::perf::mint_syscall_tp_btf(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
