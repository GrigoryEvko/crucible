// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-074-AUDIT HS14 fixture #4: `dispatch_workload_decision`
// rejects a bare `concurrent::ParallelismDecision` EVEN WHEN routed
// through the `fixy::perf::` umbrella re-export.  Mirrors
// `neg_fixy_perf_workload_decision_raw_decision` (which routes
// through `crucible::perf::*` directly) — this fixture proves the
// umbrella's using-decl carries the parameter-type gate.
//
// Why this matters: V-074-AUDIT closes the umbrella gap.  If a
// regression at the using-decl boundary erased the Tagged-only
// parameter type — for example by re-exporting a different overload
// — this fixture would compile and let band-3 code launder a bare
// decision through `crucible::fixy::perf::dispatch_workload_decision`.
//
// Distinct mismatch class (HS14 "≥2 distinct mismatch classes"):
// PARAMETER-TYPE half — Ctx is admitted (BgDrainCtx fits the
// Bg-row dispatch concept perfectly) but the decision argument's
// type doesn't match TaggedParallelismDecision.  Sibling
// `neg_fixy_perf_workload_decision_hot_fg_via_fixy.cpp` exercises
// the CONCEPT-FAILURE half through the same umbrella.
//
// Expected diagnostic: "no matching function" /
// "TaggedParallelismDecision" / "Tagged" / "cannot convert".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Perf.h>

int main() {
    crucible::effects::BgDrainCtx bg_ctx{};

    // Hand-craft a bare ParallelismDecision — exactly the anti-pattern
    // V-074 prohibits.  No profiler in sight, no source tag.  The
    // umbrella's dispatch using-decl must reject the call.
    const crucible::concurrent::ParallelismDecision raw_decision{
        .kind   = crucible::concurrent::ParallelismDecision::Kind::Sequential,
        .factor = 1,
        .numa   = crucible::concurrent::NumaPolicy::NumaIgnore,
        .tier   = crucible::concurrent::Tier::L1Resident,
    };

    // Type mismatch via the umbrella — `fixy::perf::dispatch_workload_decision`
    // expects TaggedParallelismDecision (which is also re-exported as
    // `fixy::perf::TaggedParallelismDecision`).
    crucible::fixy::perf::dispatch_workload_decision(
        bg_ctx, raw_decision,
        [](const crucible::concurrent::ParallelismDecision&) noexcept { },
        [](const crucible::concurrent::ParallelismDecision&) noexcept { });
    return 0;
}
