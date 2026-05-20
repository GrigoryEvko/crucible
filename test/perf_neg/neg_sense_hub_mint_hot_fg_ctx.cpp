// FIXY-U-083 HS14 neg-compile fixture (2 of 2 for mint_sense_hub).
//
// mint_sense_hub rejects HotFgCtx — the hot foreground context must
// never engage SenseHub::load() because the BPF program loading +
// mmap path is a multi-millisecond startup cost.  Distinct from the
// BgDrainCtx fixture: BgDrain carries Bg + Alloc but neither
// engages Init; HotFg carries no Init either AND additionally
// represents the hot-path constraint.

#include <crucible/perf/SenseHub.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub(
        crucible::effects::HotFgCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
