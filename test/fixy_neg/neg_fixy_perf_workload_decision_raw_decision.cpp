// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-074 HS14 fixture #2: `dispatch_workload_decision` rejects a
// bare `concurrent::ParallelismDecision` passed where its second
// parameter expects `TaggedParallelismDecision` (i.e.,
// `safety::Tagged<ParallelismDecision, source::WorkloadProfiler>`).
//
// Why this matters: the entire load-bearing claim of V-074 is that
// the profiler's authority over parallelism decisions is encoded in
// the TYPE — a hand-crafted `ParallelismDecision{Sequential, 1, ...}`
// constructed inline by application code must NOT be routable through
// dispatch_workload_decision.  If this fixture compiled, V-074's
// guarantee evaporates: the source::WorkloadProfiler phantom tag
// would be reduced to a hint instead of a gate.
//
// Distinct mismatch class (per HS14 "≥2 distinct mismatch classes"):
// this fixture exercises the PARAMETER-TYPE half — Ctx is admitted
// (BgDrainCtx fits CtxFitsWorkloadDecisionDispatch perfectly) but
// the decision argument's type doesn't match Tagged.  Sibling
// `neg_fixy_perf_workload_decision_hot_fg.cpp` exercises the
// CONCEPT-FAILURE half.
//
// Expected diagnostic: "no matching function" / "Tagged" /
// "ParallelismDecision" / "WorkloadProfiler".

#include <crucible/effects/ExecCtx.h>
#include <crucible/perf/WorkloadProfiler.h>

int main() {
    crucible::effects::BgDrainCtx bg_ctx{};

    // Hand-craft a bare ParallelismDecision — this is exactly the
    // anti-pattern V-074 prohibits.  No profiler in sight, no source
    // tag, no Tagged wrapper.  The dispatch must reject the call.
    const crucible::concurrent::ParallelismDecision raw_decision{
        .kind   = crucible::concurrent::ParallelismDecision::Kind::Sequential,
        .factor = 1,
        .numa   = crucible::concurrent::NumaPolicy::NumaIgnore,
        .tier   = crucible::concurrent::Tier::L1Resident,
    };

    // Type mismatch — dispatch expects TaggedParallelismDecision.
    crucible::perf::dispatch_workload_decision(
        bg_ctx, raw_decision,
        [](const crucible::concurrent::ParallelismDecision&) noexcept { },
        [](const crucible::concurrent::ParallelismDecision&) noexcept { });
    return 0;
}
