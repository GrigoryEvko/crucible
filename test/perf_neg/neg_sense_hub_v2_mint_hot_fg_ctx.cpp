// FIXY-U-083 HS14 neg-compile fixture (2 of 2 for mint_sense_hub_v2).
//
// mint_sense_hub_v2 rejects HotFgCtx — hot foreground context must
// never engage the multi-ms BPF program load + mmap startup cost.
// Distinct mismatch class from BgDrainCtx: hot-path constraint vs
// background-drain Bg+Alloc engagement.

#include <crucible/perf/SenseHubV2.h>

int main() {
    auto hub = crucible::perf::mint_sense_hub_v2(
        crucible::effects::HotFgCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
