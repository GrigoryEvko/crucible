// FIXY-U-083 HS14 neg-compile fixture (1 of 2 for mint_pmu_sample).
//
// mint_pmu_sample rejects BgDrainCtx — BgDrain carries Bg + Alloc but
// neither engages Init.  PmuSample::load() opens per-CPU
// perf_event_open file descriptors and mmaps the kernel sample ring,
// startup-only operations that must remain in the Init row.

#include <crucible/perf/PmuSample.h>

int main() {
    auto hub = crucible::perf::mint_pmu_sample(
        crucible::effects::BgDrainCtx{},
        crucible::effects::testing::init());
    (void)hub;
    return 0;
}
