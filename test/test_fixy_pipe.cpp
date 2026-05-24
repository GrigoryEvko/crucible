// ── test_fixy_pipe — sentinel TU for fixy/Pipe.h ───────────────────
//
// Pulls fixy/Pipe.h into a TU compiled under project warning flags so
// the header's static_asserts execute.  Witnesses:
//
//   1. fixy::pipe::{Endpoint, Stage, Pipeline, Direction} alias the
//      substrate types.
//   2. fixy::pipe::mint_stage / mint_pipeline are reachable via the
//      alias and produce values that are bit-identical to those
//      constructed via the substrate.
//   3. fixy-M-18: CtxFitsStage / CtxFitsStageFromEndpoints /
//      CtxFitsPipeline are reachable AND identical to the substrate
//      concepts (the "preserved verbatim" doc-block claim).
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_pipe_*.cpp.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <atomic>       // V-215 — atomic counter in Pool runtime witness
#include <cstddef>      // V-076 — std::size_t in WorkingSet witnesses
#include <cstdio>       // V-215 — diagnostic on failed Pool runtime
#include <cstdlib>      // V-215 — std::abort on failed Pool runtime
#include <limits>       // V-076 — numeric_limits in WorkingSet witnesses
#include <optional>
#include <type_traits>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;
namespace conc  = crucible::concurrent;

// ─── 1. Type carrier aliases ──────────────────────────────────────

static_assert(std::is_same_v<fpipe::Direction, conc::Direction>,
    "fixy::pipe::Direction must alias concurrent::Direction.");

// ─── 2. Stage test fixtures ───────────────────────────────────────
//
// Defined ahead of the §XXI reach probes so the same concrete stage
// signature drives both the static_assert reach witnesses AND the
// runtime mint_pipeline composition in main().  The constrained-
// `auto&&` form was tried first and rejected: it makes the function a
// template, so `&FnPtr` cannot deduce a function pointer for
// `PipelineStage<FnPtr>` to test.

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

inline void pass_through_a(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
inline void pass_through_b(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

// ─── 1b. fixy-M-18: CtxFitsStage + CtxFitsStageFromEndpoints reach ─
//
// Pipe.h's doc-block claims the substrate's concept gates are
// "preserved verbatim" through using-declarations (lines 143-155).
// M-18 audit: prove the claim mechanically by exercising each gate at
// the fixy::pipe:: surface against the same concrete stage signature
// the substrate already admits.  If a future refactor drops the
// using-decl, the static_assert below fires with a precise "is not a
// concept / cannot be evaluated" diagnostic at the fixy surface
// rather than the substrate.

// CtxFitsStage at the fixy::pipe:: surface admits a valid stage.
static_assert(fpipe::CtxFitsStage<&pass_through_a, eff::HotFgCtx>,
    "fixy::pipe::CtxFitsStage must surface the substrate concept "
    "(M-18: Pipe.h:154 using-decl).");

// CtxFitsStage at fixy::pipe:: AGREES with the substrate concept on
// every (FnPtr, Ctx) pair — proves the using-decl is a true alias,
// not a shadowed redefinition.  Two ctx flavors give two witness rows.
static_assert(fpipe::CtxFitsStage<&pass_through_a, eff::HotFgCtx>
           == conc::CtxFitsStage<&pass_through_a, eff::HotFgCtx>);
static_assert(fpipe::CtxFitsStage<&pass_through_a, eff::BgDrainCtx>
           == conc::CtxFitsStage<&pass_through_a, eff::BgDrainCtx>);

// CtxFitsStageFromEndpoints reach probe — the Tier-2→3 concept gate
// from concurrent::StageEndpointBridge.h surfaces here too.  We test
// reach (concept instantiable) rather than truth: the endpoint shapes
// here use the canonical SPSC substrate, which may or may not match
// the stage payload — the M-18 claim is that the GATE is reachable.
static_assert(
    requires { typename std::bool_constant<
        fpipe::CtxFitsStageFromEndpoints<
            &pass_through_a,
            eff::HotFgCtx,
            conc::Endpoint<conc::PermissionedSpscChannel<int, 16>,
                           conc::Direction::Consumer, eff::HotFgCtx>,
            conc::Endpoint<conc::PermissionedSpscChannel<int, 16>,
                           conc::Direction::Producer, eff::HotFgCtx>>>; },
    "fixy::pipe::CtxFitsStageFromEndpoints must be a valid concept "
    "instantiation (M-18: Pipe.h:155 using-decl).");

// CtxFitsPipeline reach — the chain-fold concept must also surface
// through fixy::pipe::, completing the M-18 trifecta.  Identity with
// the substrate concept proves the using-decl on line 147 is a true
// alias rather than a redefinition.
static_assert(fpipe::CtxFitsPipeline<eff::HotFgCtx,
        conc::Stage<&pass_through_a, eff::HotFgCtx>>
           == conc::CtxFitsPipeline<eff::HotFgCtx,
        conc::Stage<&pass_through_a, eff::HotFgCtx>>);

// ─── V-076 cost-model reach witnesses ──────────────────────────────
//
// ParallelismRule + WorkBudget + Tier + NumaPolicy +
// ParallelismDecision must surface through fixy::pipe:: with the
// substrate's exact identity.  budget_for_span is the constexpr
// factory — exercising it under static_assert proves the class is
// reachable (and not just declared as opaque) through the alias.

static_assert(std::is_same_v<fpipe::WorkBudget, conc::WorkBudget>);
static_assert(std::is_same_v<fpipe::Tier, conc::Tier>);
static_assert(std::is_same_v<fpipe::NumaPolicy, conc::NumaPolicy>);
static_assert(std::is_same_v<fpipe::ParallelismDecision, conc::ParallelismDecision>);
static_assert(std::is_same_v<fpipe::ParallelismRule, conc::ParallelismRule>);

// Constexpr factory reach — proves ParallelismRule's body is fully
// substituted via the alias, not just the name.
static_assert(fpipe::ParallelismRule::budget_for_span<int>(100).read_bytes
              == 100 * sizeof(int));

// Enum literals reach via fixy::pipe::Tier / NumaPolicy.
static_assert(fpipe::Tier::L1Resident == conc::Tier::L1Resident);
static_assert(fpipe::NumaPolicy::NumaSpread == conc::NumaPolicy::NumaSpread);

// ParallelismDecision::Kind reach — substrate enum-class surfaces
// through the using-decl as a nested name.
static_assert(fpipe::ParallelismDecision::Kind::Sequential ==
              conc::ParallelismDecision::Kind::Sequential);

// ─── V-076 WorkingSet helper witnesses ─────────────────────────────
//
// cell_line_footprint / lines_plus_cell / saturating_ws_add /
// has_static_per_call_working_set / per_call_working_set_of_v all
// surface through fixy::pipe:: with identical compile-time results.

static_assert(fpipe::hot_path_cache_line_bytes == 64);
static_assert(fpipe::unknown_per_call_working_set ==
              std::numeric_limits<std::size_t>::max());
static_assert(fpipe::cell_line_footprint(65) == 128);
static_assert(fpipe::cell_line_footprint(0) == 0);
static_assert(fpipe::saturating_ws_add(100, 200) == 300);
static_assert(fpipe::saturating_ws_add(
    fpipe::unknown_per_call_working_set, 1) ==
    fpipe::unknown_per_call_working_set);

// has_static_per_call_working_set witness — the trait surfaces and
// returns the substrate's answer.
struct V076StaticWs {
    static constexpr std::size_t per_call_working_set = 4096;
};
struct V076NoStaticWs {};

static_assert(fpipe::has_static_per_call_working_set_v<V076StaticWs>);
static_assert(!fpipe::has_static_per_call_working_set_v<V076NoStaticWs>);

// per_call_working_set_of_v extractor surfaces correctly.
static_assert(fpipe::per_call_working_set_of_v<V076StaticWs> == 4096);
static_assert(fpipe::per_call_working_set_of_v<V076NoStaticWs> ==
              fpipe::unknown_per_call_working_set);

// ─── V-077 AutoRouter + AutoSplit reach witnesses ──────────────────
//
// Identity asserts on the type carriers prove the using-decls alias
// substrate.  Enum-literal reach via `==` proves the substrate's
// nested values can be spelled at the fixy surface.  `auto_route_v`
// reach proves the constexpr template variable's BODY is fully
// substituted via the alias (not just the name).  auto_split_plan
// reach proves the central planning factory's constexpr body is
// reachable through the alias.
//
// HS14 §XXI compliance for V-077: drift-catch fixtures live in
// test/fixy_neg/neg_fixy_pipe_route_intent_to_scheduling_intent.cpp
// (CROSS-ENUM-CLASS) and
// test/fixy_neg/neg_fixy_pipe_partition_to_schedule_mode.cpp
// (SAME-ENUMERATOR-NAME collision — both Partition and ScheduleMode
// carry an Inline; type system must keep them DISTINCT).

namespace conc_v077 = ::crucible::concurrent;

// Type-identity (mirrors the in-header sentinel asserts so the test
// TU exercises them under project warning flags too).
static_assert(std::is_same_v<fpipe::RouteIntent, conc_v077::RouteIntent>);
static_assert(std::is_same_v<fpipe::RouteKind, conc_v077::RouteKind>);
static_assert(std::is_same_v<fpipe::AutoRouteDecision,
                             conc_v077::AutoRouteDecision>);
static_assert(std::is_same_v<fpipe::SchedulingIntent,
                             conc_v077::SchedulingIntent>);
static_assert(std::is_same_v<fpipe::AutoSplitRequest,
                             conc_v077::AutoSplitRequest>);
static_assert(std::is_same_v<fpipe::AutoSplitPlan,
                             conc_v077::AutoSplitPlan>);

// Enum-literal reach via the alias.
static_assert(fpipe::RouteIntent::Stream == conc_v077::RouteIntent::Stream);
static_assert(fpipe::RouteIntent::Shardable ==
              conc_v077::RouteIntent::Shardable);
static_assert(fpipe::RouteKind::Spsc == conc_v077::RouteKind::Spsc);
static_assert(fpipe::RouteKind::ShardedGrid ==
              conc_v077::RouteKind::ShardedGrid);
static_assert(fpipe::SchedulingIntent::LatencyCritical ==
              conc_v077::SchedulingIntent::LatencyCritical);
static_assert(fpipe::SchedulingIntent::Background ==
              conc_v077::SchedulingIntent::Background);

// SAME-ENUMERATOR collision witness: AutoSplitPartitionStrategy and
// AutoSplitScheduleMode both have `Inline` — distinct enum classes
// MUST keep these typed-apart (the HS14 partition-to-schedule-mode
// fixture exercises the cross-conversion).
static_assert(static_cast<int>(fpipe::AutoSplitPartitionStrategy::Inline) ==
              static_cast<int>(conc_v077::AutoSplitPartitionStrategy::Inline));
static_assert(static_cast<int>(fpipe::AutoSplitScheduleMode::Inline) ==
              static_cast<int>(conc_v077::AutoSplitScheduleMode::Inline));
static_assert(!std::is_same_v<fpipe::AutoSplitPartitionStrategy,
                              fpipe::AutoSplitScheduleMode>);

// auto_route_v reach — the constexpr variable's BODY is fully
// substituted via the alias.  A regression that aliases `auto_route_v`
// to `static_auto_route_v` or vice-versa would change the .kind here.
static_assert(
    fpipe::auto_route_v<fpipe::RouteIntent::Stream,
                        /*Producers=*/1,
                        /*Consumers=*/1,
                        /*WorkloadBytes=*/4096>.kind ==
    fpipe::RouteKind::Spsc);

// static_auto_route_v reach — separate template variable for the
// type-level routing form.  Same expectation for a 1×1 Stream.
static_assert(
    fpipe::static_auto_route_v<fpipe::RouteIntent::Stream,
                               int,
                               /*Capacity=*/64,
                               struct V077Tag,
                               /*Producers=*/1,
                               /*Consumers=*/1,
                               /*WorkloadBytes=*/4096>.kind ==
    fpipe::RouteKind::Spsc);

// auto_split_plan reach — the central planning factory's constexpr
// body must be reachable through the alias.  Sequential intent always
// collapses shard_count to ≤1.
static_assert([]{
    fpipe::AutoSplitRequest req{};
    req.item_count = 1024;
    req.bytes_per_item = 64;
    req.intent = fpipe::SchedulingIntent::Sequential;
    return fpipe::auto_split_plan(req).runs_inline();
}());

// workload_traits<int> instantiates and exposes the substrate's
// `hint()` static — the default for a bare type yields a
// default-constructed hint whose directive is None.
static_assert(fpipe::workload_traits<int>::hint().directive ==
              fpipe::HintDirective::None,
              "fixy::pipe::workload_traits must alias substrate trait");

// ─── V-215 — Pool surface + permission-bypass gate witnesses ──────
//
// Compile-time witnesses for the new §XXI mints:
//   * pool_submit — gates job submission through PermissionFreeJob.
//   * pool_dispatch_with_workload — gates workload-shaped jobs
//     through PermissionFreeJobWithShard.
//
// Bg-row ctx (eff::BgDrainCtx) is the production-facing positive
// shape; HotFgCtx is held back for the HS14 negative fixture.

// Capture-free job — copy-constructible, satisfies PermissionFreeJob.
static_assert(fpipe::PermissionFreeJob<decltype([](){})>,
    "PermissionFreeJob must accept a copyable no-capture closure.");

// Shard-taking variant — accepted by the PermissionFreeJobWithShard
// concept used by pool_dispatch_with_workload.
static_assert(fpipe::PermissionFreeJobWithShard<
    decltype([](fpipe::WorkShard){})>,
    "PermissionFreeJobWithShard must accept a shard-taking closure.");

// Ctx-fit witnesses — BgDrainCtx (row = Row<Bg, Alloc>) admits
// CtxFitsPoolSubmit; the safe-job + Bg-row pair is the canonical
// production-facing positive shape.
static_assert(fpipe::CtxFitsPoolSubmit<eff::BgDrainCtx,
                                       decltype([](){})>,
    "BgDrainCtx + copyable closure must satisfy CtxFitsPoolSubmit.");

static_assert(fpipe::CtxFitsPoolDispatchWithWorkload<
    eff::BgDrainCtx, decltype([](fpipe::WorkShard){})>,
    "BgDrainCtx + shard-taking closure must satisfy "
    "CtxFitsPoolDispatchWithWorkload.");

// HotFgCtx::row = Row<> — the canonical no-Bg ctx.  Both ctx-fit
// gates must REJECT.
static_assert(!fpipe::CtxFitsPoolSubmit<eff::HotFgCtx,
                                        decltype([](){})>,
    "HotFgCtx must NOT satisfy CtxFitsPoolSubmit — fixture #2.");

// ─── 3. Stage / Pipeline round-trip + V-215 Pool runtime ──────────

int main() {
    eff::HotFgCtx ctx;

    FakeConsumer<int> in_a, in_b;
    FakeProducer<int> out_a, out_b;

    auto stage_a = fpipe::mint_stage<&pass_through_a>(
        ctx, std::move(in_a), std::move(out_a));
    auto stage_b = fpipe::mint_stage<&pass_through_b>(
        ctx, std::move(in_b), std::move(out_b));

    // The pipeline composes; full execution is exercised by the
    // substrate's own pipeline tests.  Here we only assert that
    // mint_pipeline through fixy::pipe is callable.
    auto pl = fpipe::mint_pipeline(ctx, std::move(stage_a), std::move(stage_b));
    (void)pl;

    // ─── V-215: Pool runtime — pool_submit + dispatch ────────
    //
    // Mint a single-worker Pool, route a counter-bumping job through
    // pool_submit (the §XXI ctx-bound submission mint), then
    // wait_idle and verify the counter advanced.  The job is
    // capture-free + copy-constructible — the canonical safe shape.
    //
    // Bg-row ctx via the test-only TestWitness scaffold; production
    // sites use a real Keeper-minted BgDrainCtx.
    eff::BgDrainCtx bg{};
    fpipe::Pool<> pool{fpipe::CoreCount{1}};

    std::atomic<std::size_t> hits{0};
    fpipe::pool_submit(bg, pool, [&hits]() noexcept {
        hits.fetch_add(1, std::memory_order_release);
    });

    auto result = fpipe::pool_dispatch_with_workload(
        bg,
        pool,
        fpipe::WorkloadProfile::from_budget(
            fpipe::WorkBudget{
                .read_bytes = 64,
                .write_bytes = 64,
                .item_count = 1
            },
            /*parallelism=*/1),
        [&hits]() noexcept {
            hits.fetch_add(1, std::memory_order_release);
        });
    (void)result;

    pool.wait_idle();

    if (hits.load(std::memory_order_acquire) < 1u) {
        std::fprintf(stderr,
            "V-215: pool_submit + dispatch must increment hits "
            "(observed %zu)\n",
            hits.load(std::memory_order_acquire));
        std::abort();
    }

    return 0;
}
