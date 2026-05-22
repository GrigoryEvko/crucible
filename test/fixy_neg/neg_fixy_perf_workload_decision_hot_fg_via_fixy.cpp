// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-074-AUDIT HS14 fixture #3: `dispatch_workload_decision`
// rejects a HotFgCtx EVEN WHEN routed through the `fixy::perf::`
// umbrella re-export.  Mirrors `neg_fixy_perf_workload_decision_hot_fg`
// (which routes through `crucible::perf::*` directly) — this fixture
// proves the umbrella does not accidentally shadow / loosen the
// concept gate at the using-decl boundary.
//
// Why this matters: V-074-AUDIT closes the gap where band-3 production
// code uses `crucible::fixy::perf::dispatch_workload_decision` (the
// umbrella-discipline-respecting form).  A regression that re-exports
// a relaxed version of the concept would compile here; the gate must
// hold identically at the umbrella.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// CONCEPT half — `Ctx` is well-formed (passes IsExecCtx) but its row
// is `Row<>` (empty) and does not contain `Effect::Bg`.  Sibling
// `neg_fixy_perf_workload_decision_raw_decision_via_fixy.cpp`
// exercises the PARAMETER-TYPE half through the same umbrella.
//
// Expected diagnostic: "constraints not satisfied" /
// "CtxFitsWorkloadDecisionDispatch" / "Bg" / "row_contains".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Perf.h>

int main() {
    crucible::effects::HotFgCtx hot_ctx{};

    // Mint via the umbrella; the substrate gate (Init-row) admits
    // ColdInitCtx, not HotFgCtx.  Use the substrate's testing helper
    // to manufacture a well-formed Tagged decision via a fresh
    // profiler so the dispatch failure is unambiguously on the CTX
    // gate, not on a malformed decision.
    crucible::perf::WorkloadProfiler profiler{
        /*senses=*/nullptr, crucible::effects::testing::init()};
    const crucible::concurrent::WorkBudget budget{
        .read_bytes = 1024, .write_bytes = 1024, .item_count = 256};
    auto tagged = profiler.recommend(budget);

    // HotFgCtx fails `fixy::perf::CtxFitsWorkloadDecisionDispatch`
    // — empty Row<> does not contain Effect::Bg.  The umbrella's
    // using-decl `crucible::fixy::perf::dispatch_workload_decision`
    // must inherit the substrate's requires-clause unchanged.
    crucible::fixy::perf::dispatch_workload_decision(
        hot_ctx, tagged,
        [](const crucible::concurrent::ParallelismDecision&) noexcept { },
        [](const crucible::concurrent::ParallelismDecision&) noexcept { });
    return 0;
}
