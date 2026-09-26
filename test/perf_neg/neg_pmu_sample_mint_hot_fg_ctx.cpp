// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_pmu_sample rejects HotFgCtx.  The hot foreground context must
// never pay the per-CPU perf_event_open fan-out and the BPF program
// load.  This is a distinct mismatch class from the BgDrainCtx fixture:
// HotFgCtx claims the empty row, so it carries none of the three atoms
// the gate demands.

#include <crucible/perf/PmuSample.h>
#include <fixy/Ctx.h>

int main() {
    auto hub = crucible::perf::mint_pmu_sample(::foundation::effects::testing::foreground(),
                                               ::fixy::InitLoadCtx{::foundation::effects::testing::init()});
    (void)hub;
    return 0;
}
