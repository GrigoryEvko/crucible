// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of WorkloadProfiler is private, and
// mint_workload_profiler is its one friend.  So no caller builds a
// profiler without a context whose row holds Init.

#include <crucible/perf/WorkloadProfiler.h>

int main() {
    crucible::perf::WorkloadProfiler profiler{/*senses=*/nullptr, crucible::perf::WorkloadProfilerConfig{}};
    (void)profiler;
    return 0;
}
