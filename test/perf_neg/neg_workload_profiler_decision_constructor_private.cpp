// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The constructor of ProfiledDecision is private, and WorkloadProfiler
// is its one friend.  So no caller wraps a decision it wrote by hand
// and hands it to the dispatch as if a profiler had produced it.

#include <crucible/perf/WorkloadProfiler.h>
#include <fixy/concurrent/ParallelismRule.h>

int main() {
    const ::fixy::concurrent::ParallelismDecision raw_decision{
        .kind = ::fixy::concurrent::ParallelismDecision::Kind::Parallel,
        .factor = 64,
        .numa = ::fixy::concurrent::NumaPolicy::NumaIgnore,
        .tier = ::fixy::concurrent::Tier::DRAMBound,
    };
    const crucible::perf::ProfiledDecision forged{raw_decision};
    (void)forged;
    return 0;
}
