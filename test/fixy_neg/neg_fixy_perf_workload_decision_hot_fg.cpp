// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-074 HS14 fixture #1: `dispatch_workload_decision` rejects a
// HotFgCtx because the concept gate
// `CtxFitsWorkloadDecisionDispatch<Ctx>` requires the ctx's effect row
// to contain `Effect::Bg`.  HotFgCtx has `Row<>` (empty) — IsExecCtx
// admits but CtxOwnsCapability<Ctx, Effect::Bg> is false.
//
// Why this matters: dispatching to a parallel body launches jthreads
// (via permission_fork / NumaThreadPool) — that is intrinsically a
// Bg-row activity.  The foreground holds NO capability per CSL
// discipline (§IX), so routing a profiler decision through the
// dispatch from a HotFg call site would bypass the Permission/Bg
// gate that the entire concurrency layer rests on.
//
// Distinct mismatch class (per HS14 "≥2 distinct mismatch classes"):
// this fixture exercises the CONCEPT half — `Ctx` is well-formed
// (passes IsExecCtx) but its row does NOT contain the required
// capability.  Sibling fixture `neg_fixy_perf_workload_decision_raw_decision.cpp`
// exercises the PARAMETER-TYPE half (raw ParallelismDecision passed
// where TaggedParallelismDecision is required).
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsWorkloadDecisionDispatch" / "Bg" / "row_contains".

#include <crucible/effects/ExecCtx.h>
#include <crucible/perf/WorkloadProfiler.h>

int main() {
    crucible::effects::HotFgCtx hot_ctx{};

    // Construct a valid Tagged decision via a fresh profiler — we
    // need a well-formed FIRST argument so the failure is unambiguously
    // on the CTX gate, not on a malformed decision.
    crucible::perf::WorkloadProfiler profiler{
        /*senses=*/nullptr, crucible::effects::testing::init()};
    const crucible::concurrent::WorkBudget budget{
        .read_bytes = 1024, .write_bytes = 1024, .item_count = 256};
    auto tagged = profiler.recommend(budget);

    // HotFgCtx fails CtxFitsWorkloadDecisionDispatch — empty Row<>
    // does not contain Effect::Bg.
    crucible::perf::dispatch_workload_decision(
        hot_ctx, tagged,
        [](const crucible::concurrent::ParallelismDecision&) noexcept { },
        [](const crucible::concurrent::ParallelismDecision&) noexcept { });
    return 0;
}
