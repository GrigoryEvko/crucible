// FIXY-U-083 HS14 neg-compile fixture (1 of 2 for mint_workload_profiler).
//
// mint_workload_profiler rejects BgDrainCtx — BgDrain carries Bg +
// Alloc but neither engages Init.  WorkloadProfiler's ctor reads the
// borrowed Senses* once to capture a baseline snapshot from an
// underlying SenseHub that is itself Init-row; the profiler must
// therefore be constructed during Init, not bg-drain.

#include <crucible/perf/WorkloadProfiler.h>

int main() {
    auto wp = crucible::perf::mint_workload_profiler(
        crucible::effects::BgDrainCtx{},
        /*senses=*/nullptr,
        crucible::effects::testing::init());
    (void)wp;
    return 0;
}
