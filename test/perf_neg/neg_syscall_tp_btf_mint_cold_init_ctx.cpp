// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_syscall_tp_btf rejects the old-tree ColdInitCtx.  The load calls
// bpf(BPF_PROG_LOAD), which enters the kernel and waits while the
// verifier walks the program, so syscall_tp_btf_required_row carries
// Block.  The old-tree ColdInitCtx carries Alloc and IO and no Block.
// The old-tree init capability permits no Block either, so no widening
// takes that context through the gate.  The second argument is a valid
// startup load context, so the gate is the one reason that the compiler
// rejects the call.

#include <crucible/perf/SyscallTpBtf.h>

int main() {
    auto hub = crucible::perf::mint_syscall_tp_btf(crucible::effects::ColdInitCtx{::crucible::effects::testing::init()},
                                                   ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    (void)hub;
    return 0;
}
