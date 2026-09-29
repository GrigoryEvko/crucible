// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// dispatch_workload_decision asks for a context whose row holds Bg,
// because its parallel arm starts threads.  The hot foreground context
// has the empty row, so the gate refuses it.  The decision itself is
// well formed, so the refusal can only come from the context gate.  The
// sibling fixture neg_workload_decision_dispatch_raw_decision covers
// the other half: a context the gate admits with a decision the
// dispatch does not take.

#include <crucible/perf/WorkloadProfiler.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    auto profiler = crucible::perf::mint_workload_profiler(::fixy::ColdInitCtx{::foundation::effects::testing::init()},
                                                           /*senses=*/nullptr);
    const ::fixy::concurrent::WorkBudget budget{.read_bytes = 1024, .write_bytes = 1024, .item_count = 256};
    const auto decision = profiler.recommend(budget);

    crucible::perf::dispatch_workload_decision(
        ::foundation::effects::testing::ForegroundWitness::fg(), decision,
        [](const ::fixy::concurrent::ParallelismDecision&) noexcept {},
        [](const ::fixy::concurrent::ParallelismDecision&) noexcept {});
    return 0;
}
