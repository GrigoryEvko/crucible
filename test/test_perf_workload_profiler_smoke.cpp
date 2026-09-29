#include <crucible/perf/Senses.h>
#include <crucible/perf/WorkloadProfiler.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParallelismRule.h>
#include <foundation/effects/Effect.h>

#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using crucible::perf::ProfiledDecision;
using crucible::perf::WorkloadProfiler;
using crucible::perf::dispatch_workload_decision;
using fixy::concurrent::ParallelismDecision;
using fixy::concurrent::ParallelismRule;
using fixy::concurrent::WorkBudget;

// Every profiler in this test comes from the mint, under a cold init
// context.
[[nodiscard]] WorkloadProfiler make_profiler(const crucible::perf::Senses* senses) noexcept {
    return crucible::perf::mint_workload_profiler(::fixy::ColdInitCtx{::foundation::effects::testing::init()}, senses);
}

// The header asserts that the proof of origin carries no storage.
// Restating it here is the per-translation-unit witness that it keeps
// that across this layer.
static_assert(sizeof(ProfiledDecision) == sizeof(ParallelismDecision),
              "ProfiledDecision must collapse to sizeof(ParallelismDecision)");

static_assert(!std::is_copy_constructible_v<WorkloadProfiler>,
              "WorkloadProfiler holds a borrowed Senses* + per-instance baseline; "
              "copying produces two profilers racing on the same Senses with "
              "diverging deltas");
static_assert(!std::is_copy_assignable_v<WorkloadProfiler>, "see copy ctor rationale");
static_assert(std::is_move_constructible_v<WorkloadProfiler>, "moving a profiler into a Keeper state struct must work");
static_assert(std::is_move_assignable_v<WorkloadProfiler>, "see move-ctor rationale");

// These two constants arrive through the sense headers, and naming them
// here is what keeps the unused-constant warning quiet.
static_assert(crucible::perf::TIMELINE_MASK == 4095, "TIMELINE_MASK = TIMELINE_CAPACITY - 1; assumes power-of-two "
                                                     "capacity so slot = idx & mask is one bitwise AND");
static_assert(crucible::perf::PMU_SAMPLE_MASK == crucible::perf::PMU_SAMPLE_CAPACITY - 1,
              "PMU_SAMPLE_MASK = capacity - 1; assumes power-of-two");

}  // namespace

int main() {
    int failures = 0;

    {
        WorkloadProfiler profiler = make_profiler(/*senses=*/nullptr);

        // This budget is L3-resident, so the structural rule recommends
        // parallel.  A profiler with no telemetry must forward that
        // verbatim.
        const WorkBudget budget{
            .read_bytes = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count = 1u << 20,
        };
        const auto bare = ParallelismRule::recommend(budget);
        for (int i = 0; i < 5; ++i) {
            const auto dec = profiler.recommend_bare(budget);
            if (dec.kind != bare.kind || dec.factor != bare.factor) {
                std::fprintf(stderr,
                             "nullptr-Senses pass-through diverged: kind=%d/%d "
                             "factor=%zu/%zu on iteration %d\n",
                             static_cast<int>(dec.kind), static_cast<int>(bare.kind), dec.factor, bare.factor, i);
                ++failures;
            }
            if (profiler.last_was_demoted()) {
                std::fprintf(stderr, "nullptr-Senses must never report demotion (i=%d)\n", i);
                ++failures;
            }
        }
    }

    // The cache-tier gate fires first, so the profiler short-circuits
    // without reading any telemetry.  That must hold even with senses
    // attached.
    {
        auto senses = crucible::perf::Senses::load_subset(::fixy::InitLoadCtx{::foundation::effects::testing::init()},
                                                          crucible::perf::SensesMask{.sense_hub = true});
        WorkloadProfiler profiler = make_profiler(&senses);

        const WorkBudget tiny_budget{
            .read_bytes = 1024,  // ~L1d-resident on every modern CPU
            .write_bytes = 1024,
            .item_count = 256,
        };
        const auto dec = profiler.recommend_bare(tiny_budget);
        if (dec.kind != ParallelismDecision::Kind::Sequential) {
            std::fprintf(stderr, "L1-resident must be Sequential; got kind=%d factor=%zu\n", static_cast<int>(dec.kind),
                         dec.factor);
            ++failures;
        }
        if (profiler.last_was_demoted()) {
            std::fprintf(stderr, "L1-resident Sequential is structural, NOT a demotion\n");
            ++failures;
        }
        // A sequential decision skips telemetry, so the deltas stay at
        // zero.
        if (profiler.last_futex_wait_delta() != 0 || profiler.last_ctx_vol_delta() != 0) {
            std::fprintf(stderr, "Sequential decisions should not record telemetry deltas\n");
            ++failures;
        }
    }

    // The first call captures a baseline and later calls produce real
    // deltas.  Only the shape of that is checked here.  Driving an actual
    // demotion would need injectable telemetry, which is more than a
    // smoke test is worth.
    {
        auto senses = crucible::perf::Senses::load_subset(::fixy::InitLoadCtx{::foundation::effects::testing::init()},
                                                          crucible::perf::SensesMask{.sense_hub = true});
        WorkloadProfiler profiler = make_profiler(&senses);

        const WorkBudget l3_budget{
            .read_bytes = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count = 1u << 20,
        };

        // With no baseline yet, the first call can only return the
        // structural decision.
        const auto first = profiler.recommend_bare(l3_budget);
        if (profiler.last_was_demoted()) {
            std::fprintf(stderr, "first call cannot demote (no baseline) but did\n");
            ++failures;
        }

        // The second call has a baseline, so the deltas fill in when the
        // sense hub is attached.  Without the capability to attach it,
        // both stay at zero.
        const auto second = profiler.recommend_bare(l3_budget);
        const bool senses_attached = senses.coverage().sense_hub_attached;

        // The exact factor depends on the host topology, so it is not
        // asserted.  What is asserted is direction: the profiler may
        // demote, and may never raise the factor it was given.
        if (first.kind == ParallelismDecision::Kind::Parallel && second.kind == ParallelismDecision::Kind::Parallel
            && second.factor > first.factor) {
            std::fprintf(stderr,
                         "WorkloadProfiler must never promote (or grow factor); "
                         "first=%zu second=%zu\n",
                         first.factor, second.factor);
            ++failures;
        }
        // An unattached hub can produce neither a delta nor a demotion.
        if (!senses_attached) {
            if (profiler.last_futex_wait_delta() != 0 || profiler.last_ctx_vol_delta() != 0) {
                std::fprintf(stderr, "un-attached SenseHub must not produce non-zero deltas\n");
                ++failures;
            }
            if (profiler.last_was_demoted()) {
                std::fprintf(stderr, "un-attached SenseHub must not produce demotion\n");
                ++failures;
            }
        }
    }

    {
        WorkloadProfiler profiler = make_profiler(/*senses=*/nullptr);
        if (profiler.last_was_demoted() || profiler.last_futex_wait_delta() != 0
            || profiler.last_ctx_vol_delta() != 0) {
            std::fprintf(stderr, "fresh profiler diagnostics not at zero\n");
            ++failures;
        }
        const auto cfg = profiler.config();
        if (cfg.futex_wait_demote_threshold == 0 || cfg.ctx_vol_demote_threshold == 0) {
            std::fprintf(stderr, "default thresholds should be non-zero\n");
            ++failures;
        }
    }

    {
        WorkloadProfiler profiler = make_profiler(/*senses=*/nullptr);
        const WorkBudget budget{
            .read_bytes = 8 * 1024 * 1024,
            .write_bytes = 8 * 1024 * 1024,
            .item_count = 1u << 20,
        };
        (void)profiler.recommend_bare(budget);
        (void)profiler.recommend_bare(budget);
        profiler.reset();
        if (profiler.last_was_demoted() || profiler.last_futex_wait_delta() != 0
            || profiler.last_ctx_vol_delta() != 0) {
            std::fprintf(stderr, "reset() must zero diagnostics\n");
            ++failures;
        }
    }

    {
        WorkloadProfiler profiler_src = make_profiler(/*senses=*/nullptr);
        WorkloadProfiler profiler_sink = std::move(profiler_src);
        const WorkBudget budget{
            .read_bytes = 1024,
            .write_bytes = 1024,
            .item_count = 256,
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

    // The profiled form is the production-facing surface.  The proof of
    // origin carries no storage, so every field of the decision must
    // come through it unchanged.
    {
        WorkloadProfiler profiler = make_profiler(/*senses=*/nullptr);

        const WorkBudget tiny_budget{
            .read_bytes = 1024,
            .write_bytes = 1024,
            .item_count = 256,
        };

        const ProfiledDecision profiled = profiler.recommend(tiny_budget);
        if (profiled.value().kind != ParallelismDecision::Kind::Sequential) {
            std::fprintf(stderr,
                         "recommend() on an L1-resident budget must yield a profiled "
                         "Sequential; got kind=%d via .value()\n",
                         static_cast<int>(profiled.value().kind));
            ++failures;
        }

        // The reset clears the baseline, so the bare call sees the same
        // structural state the profiled one did.
        profiler.reset();
        const auto bare = profiler.recommend_bare(tiny_budget);
        if (bare.kind != profiled.value().kind || bare.factor != profiled.value().factor) {
            std::fprintf(stderr,
                         "recommend() and recommend_bare() must agree on "
                         "structural decision; bare=(%d,%zu) profiled=(%d,%zu)\n",
                         static_cast<int>(bare.kind), bare.factor, static_cast<int>(profiled.value().kind),
                         profiled.value().factor);
            ++failures;
        }

        // The dispatch must reach the sequential body and hand it the
        // bare decision.
        bool seq_fired = false;
        bool par_fired = false;
        ParallelismDecision::Kind observed_kind = ParallelismDecision::Kind::Parallel;
        const ::fixy::BgDrainCtx bg_ctx{::foundation::effects::testing::bg()};
        // A fresh decision keeps this check independent of the one
        // above.
        const ProfiledDecision profiled2 = profiler.recommend(tiny_budget);
        dispatch_workload_decision(
            bg_ctx, profiled2,
            /*seq_body=*/
            [&](const ParallelismDecision& d) noexcept {
                seq_fired = true;
                observed_kind = d.kind;
            },
            /*par_body=*/
            [&](const ParallelismDecision& d) noexcept {
                par_fired = true;
                observed_kind = d.kind;
            });
        if (!seq_fired) {
            std::fprintf(stderr,
                         "dispatch_workload_decision must invoke seq_body "
                         "for Sequential decision (seq_fired=%d, par_fired=%d)\n",
                         seq_fired, par_fired);
            ++failures;
        }
        if (par_fired) {
            std::fprintf(stderr, "dispatch_workload_decision must not invoke par_body "
                                 "for Sequential decision\n");
            ++failures;
        }
        if (observed_kind != ParallelismDecision::Kind::Sequential) {
            std::fprintf(stderr,
                         "dispatch_workload_decision body received the wrong kind: "
                         "%d (expected Sequential=%d)\n",
                         static_cast<int>(observed_kind), static_cast<int>(ParallelismDecision::Kind::Sequential));
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("perf::WorkloadProfiler smoke OK\n");
    }
    return failures == 0 ? 0 : 1;
}
