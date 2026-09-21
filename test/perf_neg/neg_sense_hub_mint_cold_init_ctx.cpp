// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sense_hub rejects ColdInitCtx.  The load calls
// bpf(BPF_PROG_LOAD), which enters the kernel and waits while the
// verifier walks the program, so sense_hub_required_row carries Block.
// ColdInitCtx carries Alloc and IO and no Block.  The initialization
// capability permits no Block either, so no widening rescues it.

#include <crucible/perf/SenseHub.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub(crucible::effects::ColdInitCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
