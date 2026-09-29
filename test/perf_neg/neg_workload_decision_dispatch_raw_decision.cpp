// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// dispatch_workload_decision takes a ProfiledDecision, which only a
// profiler builds.  This fixture writes a decision by hand and passes
// it with a context the gate admits, so the refusal can only come from
// the parameter type.  If it compiled, any caller could skip the
// profiler and its demotion.  The sibling fixture
// neg_workload_decision_dispatch_hot_fg covers the context gate.

#include <crucible/perf/WorkloadProfiler.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <foundation/effects/Effect.h>

int main() {
    const ::fixy::BgDrainCtx bg_ctx{::foundation::effects::testing::bg()};
    const ::fixy::concurrent::ParallelismDecision raw_decision{
        .kind = ::fixy::concurrent::ParallelismDecision::Kind::Parallel,
        .factor = 64,
        .numa = ::fixy::concurrent::NumaPolicy::NumaIgnore,
        .tier = ::fixy::concurrent::Tier::DRAMBound,
    };

    crucible::perf::dispatch_workload_decision(
        bg_ctx, raw_decision, [](const ::fixy::concurrent::ParallelismDecision&) noexcept {},
        [](const ::fixy::concurrent::ParallelismDecision&) noexcept {});
    return 0;
}
