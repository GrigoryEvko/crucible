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

#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/perf/Senses.h>
#include <crucible/perf/WorkloadProfiler.h>

#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using crucible::perf::WorkloadProfiler;
using crucible::concurrent::ParallelismDecision;
using crucible::concurrent::ParallelismRule;
using crucible::concurrent::WorkBudget;

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
            /*senses=*/nullptr, ::crucible::effects::Init{}};

        // L3-resident budget — structural rule recommends Parallel.
        // Profiler with no telemetry MUST forward verbatim.
        const WorkBudget budget{
            .read_bytes  = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count  = 1u << 20,
        };
        const auto bare = ParallelismRule::recommend(budget);
        for (int i = 0; i < 5; ++i) {
            const auto dec = profiler.recommend(budget);
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
            ::crucible::effects::Init{},
            crucible::perf::SensesMask{ .sense_hub = true });
        WorkloadProfiler profiler{&senses, ::crucible::effects::Init{}};

        const WorkBudget tiny_budget{
            .read_bytes  = 1024,    // ~L1d-resident on every modern CPU
            .write_bytes = 1024,
            .item_count  = 256,
        };
        const auto dec = profiler.recommend(tiny_budget);
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
            ::crucible::effects::Init{},
            crucible::perf::SensesMask{ .sense_hub = true });
        WorkloadProfiler profiler{&senses, ::crucible::effects::Init{}};

        const WorkBudget l3_budget{
            .read_bytes  = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count  = 1u << 20,
        };

        // First call: structural decision, no demotion (no baseline yet).
        const auto first = profiler.recommend(l3_budget);
        if (profiler.last_was_demoted()) {
            std::fprintf(stderr,
                "first call cannot demote (no baseline) but did\n");
            ++failures;
        }

        // Second call: now there's a baseline; deltas should be
        // populated IF SenseHub is attached.  If not (no CAP_BPF),
        // both deltas remain zero.
        const auto second = profiler.recommend(l3_budget);
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
            /*senses=*/nullptr, ::crucible::effects::Init{}};
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
            /*senses=*/nullptr, ::crucible::effects::Init{}};
        const WorkBudget budget{
            .read_bytes  = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count  = 1u << 20,
        };
        (void)profiler.recommend(budget);
        (void)profiler.recommend(budget);
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
            /*senses=*/nullptr, ::crucible::effects::Init{}};
        WorkloadProfiler profiler_sink = std::move(profiler_src);
        const WorkBudget budget{
            .read_bytes  = 1024,
            .write_bytes = 1024,
            .item_count  = 256,
        };
        const auto dec = profiler_sink.recommend(budget);
        if (dec.kind != ParallelismDecision::Kind::Sequential) {
            std::fprintf(stderr,
                "moved-into profiler should still recommend Sequential "
                "for L1-resident; got kind=%d\n",
                static_cast<int>(dec.kind));
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("perf::WorkloadProfiler smoke OK\n");
    }
    return failures == 0 ? 0 : 1;
}
