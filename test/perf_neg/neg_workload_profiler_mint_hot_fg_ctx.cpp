// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_workload_profiler asks for a context whose row holds Init.  The
// hot foreground context has the empty row, so the gate refuses it.  It
// is a different mismatch from the background fixture: that context
// claims effects, just not Init, and this one claims none.

#include <crucible/perf/WorkloadProfiler.h>
#include <foundation/effects/Ctx.h>

int main() {
    auto profiler = crucible::perf::mint_workload_profiler(::foundation::effects::testing::ForegroundWitness::fg(),
                                                           /*senses=*/nullptr);
    (void)profiler;
    return 0;
}
