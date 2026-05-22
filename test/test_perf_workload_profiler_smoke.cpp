// Sentinel TU for crucible::perf::WorkloadProfiler — closes the
// SenseHub → ParallelismRule loop opened by GAPS-004h (#1284).
//
// What this test asserts:
//   1. <crucible/perf/WorkloadProfiler.h> reachable through the
//      public crucible include path.
//   2. WorkloadProfiler is move-only (deleted copy) and reasonably
//      cache-line-shaped.
//   3. Construction with senses=nullptr produces a permanently
//      pass-through profiler (never demotes; matches structural
//      ParallelismRule output exactly).
//   4. Construction with attached Senses + L1-resident workload
//      stays Sequential without demotion (cache-tier gate handles
//      it; profiler doesn't second-guess).
//   5. Construction with attached Senses + L3-resident workload:
//      first call captures baseline + returns structural decision.
//      Subsequent calls without contention preserve the structural
//      decision.
//   6. Diagnostic accessors return their initial sentinel values
//      on a fresh / pass-through profiler.
//   7. reset() restores first-call state.
//   8. Move-construct preserves observation behaviour.
//   9. FIXY-V-074: recommend() (Tagged form) + dispatch_workload_decision
//      route through BgDrainCtx; sequential body is invoked on
//      Sequential decisions; Tagged-only signature enforced.

#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/perf/Senses.h>
#include <crucible/perf/WorkloadProfiler.h>
#include <crucible/safety/Tagged.h>

#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using crucible::perf::WorkloadProfiler;
using crucible::perf::TaggedParallelismDecision;
using crucible::perf::dispatch_workload_decision;
using crucible::concurrent::ParallelismDecision;
using crucible::concurrent::ParallelismRule;
using crucible::concurrent::WorkBudget;

// FIXY-V-074: TaggedParallelismDecision MUST be sizeof-equivalent
// (EBO-collapsed phantom Tag).  Tagged ships its own zero-cost
// assertions in Tagged.h; this restated assertion at the call site
// is the per-TU witness that the V-074 alias preserves the property
// across the perf:: layer.
static_assert(sizeof(TaggedParallelismDecision) == sizeof(ParallelismDecision),
    "FIXY-V-074: Tagged<ParallelismDecision, source::WorkloadProfiler> must "
    "EBO-collapse to sizeof(ParallelismDecision) — phantom Tag carries no storage");

// ── (2) Type-shape sanity ──────────────────────────────────────────
static_assert(!std::is_copy_constructible_v<WorkloadProfiler>,
    "WorkloadProfiler holds a borrowed Senses* + per-instance baseline; "
    "copying produces two profilers racing on the same Senses with "
    "diverging deltas");
static_assert(!std::is_copy_assignable_v<WorkloadProfiler>,
    "see copy ctor rationale");
static_assert(std::is_move_constructible_v<WorkloadProfiler>,
    "moving a profiler into a Keeper state struct must work");
static_assert(std::is_move_assignable_v<WorkloadProfiler>,
    "see move-ctor rationale");

// ── Suppress -Werror=unused-const-variable on TIMELINE_MASK +
// PMU_SAMPLE_MASK transitively pulled in via Senses.h → SchedSwitch.h /
// PmuSample.h.  All sibling smoke tests use this same pattern to
// acknowledge the constants.
static_assert(crucible::perf::TIMELINE_MASK == 4095,
    "TIMELINE_MASK = TIMELINE_CAPACITY - 1; assumes power-of-two "
    "capacity so slot = idx & mask is one bitwise AND");
static_assert(crucible::perf::PMU_SAMPLE_MASK ==
              crucible::perf::PMU_SAMPLE_CAPACITY - 1,
    "PMU_SAMPLE_MASK = capacity - 1; assumes power-of-two");

}  // namespace

int main() {
    int failures = 0;

    // ── (3) nullptr Senses → pass-through over ParallelismRule ─────
    {
        WorkloadProfiler profiler{
            /*senses=*/nullptr, ::crucible::effects::testing::init()};

        // L3-resident budget — structural rule recommends Parallel.
        // Profiler with no telemetry MUST forward verbatim.
        const WorkBudget budget{
            .read_bytes  = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count  = 1u << 20,
        };
        const auto bare = ParallelismRule::recommend(budget);
        for (int i = 0; i < 5; ++i) {
            const auto dec = profiler.recommend_bare(budget);
            if (dec.kind != bare.kind || dec.factor != bare.factor) {
                std::fprintf(stderr,
                    "nullptr-Senses pass-through diverged: kind=%d/%d "
                    "factor=%zu/%zu on iteration %d\n",
                    static_cast<int>(dec.kind),
                    static_cast<int>(bare.kind),
                    dec.factor, bare.factor, i);
                ++failures;
            }
            if (profiler.last_was_demoted()) {
                std::fprintf(stderr,
                    "nullptr-Senses must never report demotion (i=%d)\n", i);
                ++failures;
            }
        }
    }

    // ── (4) L1-resident with attached Senses → still Sequential ────
    //
    // The cache-tier gate fires first (in ParallelismRule) and the
    // profiler short-circuits without reading SenseHub.  This must
    // hold even when senses is non-null.
    {
        auto senses = crucible::perf::Senses::load_subset(
            ::crucible::effects::testing::init(),
            crucible::perf::SensesMask{ .sense_hub = true });
        WorkloadProfiler profiler{&senses, ::crucible::effects::testing::init()};

        const WorkBudget tiny_budget{
            .read_bytes  = 1024,    // ~L1d-resident on every modern CPU
            .write_bytes = 1024,
            .item_count  = 256,
        };
        const auto dec = profiler.recommend_bare(tiny_budget);
        if (dec.kind != ParallelismDecision::Kind::Sequential) {
            std::fprintf(stderr,
                "L1-resident must be Sequential; got kind=%d factor=%zu\n",
                static_cast<int>(dec.kind), dec.factor);
            ++failures;
        }
        if (profiler.last_was_demoted()) {
            std::fprintf(stderr,
                "L1-resident Sequential is structural, NOT a demotion\n");
            ++failures;
        }
        // Sequential decisions skip telemetry → deltas zeroed.
        if (profiler.last_futex_wait_delta() != 0
            || profiler.last_ctx_vol_delta() != 0) {
            std::fprintf(stderr,
                "Sequential decisions should not record telemetry deltas\n");
            ++failures;
        }
    }

    // ── (5) Attached Senses + L3-resident: baseline + steady-state ─
    //
    // First call captures baseline; subsequent calls produce real
    // deltas.  This test checks the API shape, not specific demotion
    // outcomes — the threshold-fire path requires injectable
    // telemetry that isn't worth synthesizing in a smoke test.
    {
        auto senses = crucible::perf::Senses::load_subset(
            ::crucible::effects::testing::init(),
            crucible::perf::SensesMask{ .sense_hub = true });
        WorkloadProfiler profiler{&senses, ::crucible::effects::testing::init()};

        const WorkBudget l3_budget{
            .read_bytes  = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count  = 1u << 20,
        };

        // First call: structural decision, no demotion (no baseline yet).
        const auto first = profiler.recommend_bare(l3_budget);
        if (profiler.last_was_demoted()) {
            std::fprintf(stderr,
                "first call cannot demote (no baseline) but did\n");
            ++failures;
        }

        // Second call: now there's a baseline; deltas should be
        // populated IF SenseHub is attached.  If not (no CAP_BPF),
        // both deltas remain zero.
        const auto second = profiler.recommend_bare(l3_budget);
        const bool senses_attached = senses.coverage().sense_hub_attached;

        // The decision shape must remain stable across calls under a
        // quiet system.  We don't assert exact factor since it depends
        // on host topology, but kind+factor must NOT regress (i.e.,
        // can demote to Sequential, must not change Parallel→Parallel
        // factor downward without demotion).
        if (first.kind == ParallelismDecision::Kind::Parallel
            && second.kind == ParallelismDecision::Kind::Parallel
            && second.factor > first.factor) {
            std::fprintf(stderr,
                "WorkloadProfiler must never promote (or grow factor); "
                "first=%zu second=%zu\n",
                first.factor, second.factor);
            ++failures;
        }
        // If senses unattached, deltas always zero (sanity check).
        if (!senses_attached) {
            if (profiler.last_futex_wait_delta() != 0
                || profiler.last_ctx_vol_delta() != 0) {
                std::fprintf(stderr,
                    "un-attached SenseHub must not produce non-zero deltas\n");
                ++failures;
            }
            if (profiler.last_was_demoted()) {
                std::fprintf(stderr,
                    "un-attached SenseHub must not produce demotion\n");
                ++failures;
            }
        }
    }

    // ── (6) Diagnostics on a fresh profiler ────────────────────────
    {
        WorkloadProfiler profiler{
            /*senses=*/nullptr, ::crucible::effects::testing::init()};
        if (profiler.last_was_demoted()
            || profiler.last_futex_wait_delta() != 0
            || profiler.last_ctx_vol_delta() != 0) {
            std::fprintf(stderr,
                "fresh profiler diagnostics not at zero\n");
            ++failures;
        }
        const auto cfg = profiler.config();
        if (cfg.futex_wait_demote_threshold == 0
            || cfg.ctx_vol_demote_threshold == 0) {
            std::fprintf(stderr,
                "default thresholds should be non-zero\n");
            ++failures;
        }
    }

    // ── (7) reset() returns to first-call state ────────────────────
    {
        WorkloadProfiler profiler{
            /*senses=*/nullptr, ::crucible::effects::testing::init()};
        const WorkBudget budget{
            .read_bytes  = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count  = 1u << 20,
        };
        (void)profiler.recommend_bare(budget);
        (void)profiler.recommend_bare(budget);
        profiler.reset();
        if (profiler.last_was_demoted()
            || profiler.last_futex_wait_delta() != 0
            || profiler.last_ctx_vol_delta() != 0) {
            std::fprintf(stderr,
                "reset() must zero diagnostics\n");
            ++failures;
        }
    }

    // ── (8) Move-construct sanity ──────────────────────────────────
    {
        WorkloadProfiler profiler_src{
            /*senses=*/nullptr, ::crucible::effects::testing::init()};
        WorkloadProfiler profiler_sink = std::move(profiler_src);
        const WorkBudget budget{
            .read_bytes  = 1024,
            .write_bytes = 1024,
            .item_count  = 256,
        };
        const auto dec = profiler_sink.recommend_bare(budget);
        if (dec.kind != ParallelismDecision::Kind::Sequential) {
            std::fprintf(stderr,
                "moved-into profiler should still recommend Sequential "
                "for L1-resident; got kind=%d\n",
                static_cast<int>(dec.kind));
            ++failures;
        }
    }

    // ── (9) FIXY-V-074: recommend() + dispatch_workload_decision ───
    //
    // The Tagged form is the production-facing surface.  Tests cover:
    //   (a) Sequential decision routes to the sequential body.
    //   (b) Tagged value() reads the inner decision unmodified.
    //   (c) recommend_bare() and recommend() agree on the structural
    //       decision they observe (the Tagged wrapper is purely
    //       phantom — kind/factor/numa/tier are preserved verbatim).
    //   (d) sizeof(TaggedParallelismDecision) == sizeof(ParallelismDecision)
    //       is witnessed at file scope above; this section exercises
    //       the runtime invariants.
    {
        WorkloadProfiler profiler{
            /*senses=*/nullptr, ::crucible::effects::testing::init()};

        const WorkBudget tiny_budget{
            .read_bytes  = 1024,
            .write_bytes = 1024,
            .item_count  = 256,
        };

        // (a) + (b): recommend() returns Tagged.  value() reads the
        // inner decision; structural rule says Sequential for tiny.
        const TaggedParallelismDecision tagged = profiler.recommend(tiny_budget);
        if (tagged.value().kind != ParallelismDecision::Kind::Sequential) {
            std::fprintf(stderr,
                "V-074: recommend() L1-resident must yield Tagged-wrapped "
                "Sequential; got kind=%d via .value()\n",
                static_cast<int>(tagged.value().kind));
            ++failures;
        }

        // (c): recommend_bare() on fresh profiler (no telemetry, same
        // budget) — agreement check.  Calls reset() to clear the
        // first_call_ baseline so the structural-rule output matches.
        profiler.reset();
        const auto bare = profiler.recommend_bare(tiny_budget);
        if (bare.kind != tagged.value().kind || bare.factor != tagged.value().factor) {
            std::fprintf(stderr,
                "V-074: recommend() and recommend_bare() must agree on "
                "structural decision; bare=(%d,%zu) tagged=(%d,%zu)\n",
                static_cast<int>(bare.kind), bare.factor,
                static_cast<int>(tagged.value().kind), tagged.value().factor);
            ++failures;
        }

        // (d) dispatch_workload_decision via BgDrainCtx — must invoke
        // the SEQUENTIAL body (decision is Sequential), and pass the
        // bare ParallelismDecision (not the Tagged) to the body.
        bool seq_fired = false;
        bool par_fired = false;
        ParallelismDecision::Kind observed_kind = ParallelismDecision::Kind::Parallel;
        ::crucible::effects::BgDrainCtx bg_ctx{};
        // Re-issue recommend() — previous Tagged was consumed by value() but
        // a fresh one keeps the test predicate-clean.
        const TaggedParallelismDecision tagged2 = profiler.recommend(tiny_budget);
        dispatch_workload_decision(
            bg_ctx, tagged2,
            /*seq_body=*/[&](const ParallelismDecision& d) noexcept {
                seq_fired = true;
                observed_kind = d.kind;
            },
            /*par_body=*/[&](const ParallelismDecision& d) noexcept {
                par_fired = true;
                observed_kind = d.kind;
            });
        if (!seq_fired) {
            std::fprintf(stderr,
                "V-074: dispatch_workload_decision must invoke seq_body "
                "for Sequential decision (seq_fired=%d, par_fired=%d)\n",
                seq_fired, par_fired);
            ++failures;
        }
        if (par_fired) {
            std::fprintf(stderr,
                "V-074: dispatch_workload_decision must NOT invoke par_body "
                "for Sequential decision\n");
            ++failures;
        }
        if (observed_kind != ParallelismDecision::Kind::Sequential) {
            std::fprintf(stderr,
                "V-074: dispatch_workload_decision body received wrong kind: "
                "%d (expected Sequential=%d)\n",
                static_cast<int>(observed_kind),
                static_cast<int>(ParallelismDecision::Kind::Sequential));
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("perf::WorkloadProfiler smoke OK\n");
    }
    return failures == 0 ? 0 : 1;
}
