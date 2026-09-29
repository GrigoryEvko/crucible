// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_workload_profiler asks for a context whose row holds Init,
// because a profiler is built at process startup.  The background drain
// context claims Bg and Alloc, so the gate refuses it.

#include <crucible/perf/WorkloadProfiler.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    auto profiler = crucible::perf::mint_workload_profiler(::fixy::BgDrainCtx{::foundation::effects::testing::bg()},
                                                           /*senses=*/nullptr);
    (void)profiler;
    return 0;
}
