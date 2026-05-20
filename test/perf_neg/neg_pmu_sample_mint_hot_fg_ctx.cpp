// FIXY-U-083 HS14 neg-compile fixture (2 of 2 for mint_pmu_sample).
//
// mint_pmu_sample rejects HotFgCtx — hot foreground context must never
// engage the multi-syscall perf_event_open + mmap startup cost.
// Distinct mismatch class from BgDrainCtx: hot-path constraint vs
// background-drain Bg+Alloc engagement.

#include <crucible/perf/PmuSample.h>

int main() {
    auto hub = crucible::perf::mint_pmu_sample(
        crucible::effects::HotFgCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
