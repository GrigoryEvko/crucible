// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ProfiledDecision is not trivially copyable, so std::bit_cast cannot
// build one from the bytes of a decision written by hand.  The private
// constructor alone would not stop that route.

#include <crucible/perf/WorkloadProfiler.h>
#include <fixy/concurrent/ParallelismRule.h>

#include <bit>

int main() {
    const ::fixy::concurrent::ParallelismDecision raw_decision{
        .kind = ::fixy::concurrent::ParallelismDecision::Kind::Parallel,
        .factor = 64,
        .numa = ::fixy::concurrent::NumaPolicy::NumaIgnore,
        .tier = ::fixy::concurrent::Tier::DRAMBound,
    };
    const auto forged = std::bit_cast<crucible::perf::ProfiledDecision>(raw_decision);
    (void)forged;
    return 0;
}
