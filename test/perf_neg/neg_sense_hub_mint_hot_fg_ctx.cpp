// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_sense_hub rejects HotFgCtx.  The hot foreground context must
// never reach SenseHub::load(), because the BPF program load and the
// mmap path cost milliseconds.  This is a distinct mismatch class from
// the BgDrainCtx fixture: HotFgCtx claims the empty row, so it carries
// none of the three atoms the gate demands, and the foreground
// capability permits none of them either.

#include <crucible/perf/SenseHub.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub(crucible::effects::HotFgCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
