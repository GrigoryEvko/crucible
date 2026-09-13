// Pulls the pipe umbrella into a translation unit compiled under the
// project warning flags, so the header's own static_asserts execute.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace eff = crucible::effects;
namespace fpipe = crucible::fixy::pipe;
namespace conc = crucible::concurrent;

static_assert(std::is_same_v<fpipe::Direction, conc::Direction>,
              "fixy::pipe::Direction must alias concurrent::Direction.");

// These come first so one concrete stage signature drives both the
// compile-time probes and the composition in main().  The constrained
// `auto&&` parameter form loses here: it turns the function into a
// template, and then `&FnPtr` cannot deduce a function pointer.

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

// The umbrella re-exports the substrate's concept gates through
// using-declarations.  Exercising each gate at the umbrella surface
// against a signature the substrate already admits means a dropped
// using-declaration reports itself here rather than at the substrate.
static_assert(fpipe::CtxFitsStage<&pass_through_a, eff::HotFgCtx>,
              "fixy::pipe::CtxFitsStage must surface the substrate concept.");

// Agreement on every pair proves a true alias rather than a shadowing
// redefinition.
static_assert(fpipe::CtxFitsStage<&pass_through_a, eff::HotFgCtx>
              == conc::CtxFitsStage<&pass_through_a, eff::HotFgCtx>);
static_assert(fpipe::CtxFitsStage<&pass_through_a, eff::BgDrainCtx>
              == conc::CtxFitsStage<&pass_through_a, eff::BgDrainCtx>);

// This probe asks whether the concept can be instantiated, not whether
// it holds.  The endpoint shapes here need not match the stage payload,
// because the claim is only that the gate is reachable.
static_assert(
    requires {
        typename std::bool_constant<fpipe::CtxFitsStageFromEndpoints<
            &pass_through_a, eff::HotFgCtx,
            conc::Endpoint<conc::PermissionedSpscChannel<int, 16>, conc::Direction::Consumer, eff::HotFgCtx>,
            conc::Endpoint<conc::PermissionedSpscChannel<int, 16>, conc::Direction::Producer, eff::HotFgCtx>>>;
    }, "fixy::pipe::CtxFitsStageFromEndpoints must be a valid concept "
       "instantiation.");

static_assert(fpipe::CtxFitsPipeline<eff::HotFgCtx, conc::Stage<&pass_through_a, eff::HotFgCtx>>
              == conc::CtxFitsPipeline<eff::HotFgCtx, conc::Stage<&pass_through_a, eff::HotFgCtx>>);

static_assert(std::is_same_v<fpipe::WorkBudget, conc::WorkBudget>);
static_assert(std::is_same_v<fpipe::Tier, conc::Tier>);
static_assert(std::is_same_v<fpipe::NumaPolicy, conc::NumaPolicy>);
static_assert(std::is_same_v<fpipe::ParallelismDecision, conc::ParallelismDecision>);
static_assert(std::is_same_v<fpipe::ParallelismRule, conc::ParallelismRule>);

// Calling the factory proves the alias carries the whole class body
// through, not just the name.
static_assert(fpipe::ParallelismRule::budget_for_span<int>(100).read_bytes == 100 * sizeof(int));

static_assert(fpipe::Tier::L1Resident == conc::Tier::L1Resident);
static_assert(fpipe::NumaPolicy::NumaSpread == conc::NumaPolicy::NumaSpread);

static_assert(fpipe::ParallelismDecision::Kind::Sequential == conc::ParallelismDecision::Kind::Sequential);

static_assert(fpipe::hot_path_cache_line_bytes == 64);
static_assert(fpipe::unknown_per_call_working_set == std::numeric_limits<std::size_t>::max());
static_assert(fpipe::cell_line_footprint(65) == 128);
static_assert(fpipe::cell_line_footprint(0) == 0);
static_assert(fpipe::saturating_ws_add(100, 200) == 300);
static_assert(fpipe::saturating_ws_add(fpipe::unknown_per_call_working_set, 1) == fpipe::unknown_per_call_working_set);

struct V076StaticWs {
    static constexpr std::size_t per_call_working_set = 4096;
};
struct V076NoStaticWs {};

static_assert(fpipe::has_static_per_call_working_set_v<V076StaticWs>);
static_assert(!fpipe::has_static_per_call_working_set_v<V076NoStaticWs>);

static_assert(fpipe::per_call_working_set_of_v<V076StaticWs> == 4096);
static_assert(fpipe::per_call_working_set_of_v<V076NoStaticWs> == fpipe::unknown_per_call_working_set);

namespace conc_v077 = ::crucible::concurrent;

static_assert(std::is_same_v<fpipe::RouteIntent, conc_v077::RouteIntent>);
static_assert(std::is_same_v<fpipe::RouteKind, conc_v077::RouteKind>);
static_assert(std::is_same_v<fpipe::AutoRouteDecision, conc_v077::AutoRouteDecision>);
static_assert(std::is_same_v<fpipe::SchedulingIntent, conc_v077::SchedulingIntent>);
static_assert(std::is_same_v<fpipe::AutoSplitRequest, conc_v077::AutoSplitRequest>);
static_assert(std::is_same_v<fpipe::AutoSplitPlan, conc_v077::AutoSplitPlan>);

static_assert(fpipe::RouteIntent::Stream == conc_v077::RouteIntent::Stream);
static_assert(fpipe::RouteIntent::Shardable == conc_v077::RouteIntent::Shardable);
static_assert(fpipe::RouteKind::Spsc == conc_v077::RouteKind::Spsc);
static_assert(fpipe::RouteKind::ShardedGrid == conc_v077::RouteKind::ShardedGrid);
static_assert(fpipe::SchedulingIntent::LatencyCritical == conc_v077::SchedulingIntent::LatencyCritical);
static_assert(fpipe::SchedulingIntent::Background == conc_v077::SchedulingIntent::Background);

// Both enumerations carry an Inline enumerator.  They are distinct enum
// classes and must stay typed apart.
static_assert(static_cast<int>(fpipe::AutoSplitPartitionStrategy::Inline)
              == static_cast<int>(conc_v077::AutoSplitPartitionStrategy::Inline));
static_assert(static_cast<int>(fpipe::AutoSplitScheduleMode::Inline)
              == static_cast<int>(conc_v077::AutoSplitScheduleMode::Inline));
static_assert(!std::is_same_v<fpipe::AutoSplitPartitionStrategy, fpipe::AutoSplitScheduleMode>);

// Evaluating the variable proves the alias carries its body through.
// Aliasing it to the type-level form below would change the kind here.
static_assert(
    fpipe::auto_route_v<fpipe::RouteIntent::Stream,
                        /*Producers=*/1,
                        /*Consumers=*/1,
                        /*WorkloadBytes=*/4096>.kind ==
    fpipe::RouteKind::Spsc);

// The type-level routing form is a separate variable, with the same
// answer for a one-producer one-consumer stream.
static_assert(
    fpipe::static_auto_route_v<fpipe::RouteIntent::Stream,
                               int,
                               /*Capacity=*/64,
                               struct V077Tag,
                               /*Producers=*/1,
                               /*Consumers=*/1,
                               /*WorkloadBytes=*/4096>.kind ==
    fpipe::RouteKind::Spsc);

// A sequential intent collapses the shard count to one at most, so the
// plan must run inline.
static_assert([] {
    fpipe::AutoSplitRequest req{};
    req.item_count = 1024;
    req.bytes_per_item = 64;
    req.intent = fpipe::SchedulingIntent::Sequential;
    return fpipe::auto_split_plan(req).runs_inline();
}());

// A bare type takes the default hint, whose directive is None.
static_assert(fpipe::workload_traits<int>::hint().directive == fpipe::HintDirective::None,
              "fixy::pipe::workload_traits must alias substrate trait");

// A background-row context is the production-facing positive shape.  The
// hot foreground context is kept for the negative case below.
static_assert(fpipe::PermissionFreeJob<decltype([]() {})>,
              "PermissionFreeJob must accept a copyable no-capture closure.");

static_assert(fpipe::PermissionFreeJobWithShard<decltype([](fpipe::WorkShard) {})>,
              "PermissionFreeJobWithShard must accept a shard-taking closure.");

// This context carries Bg and Alloc in its row.
static_assert(fpipe::CtxFitsPoolSubmit<eff::BgDrainCtx, decltype([]() {})>,
              "BgDrainCtx + copyable closure must satisfy CtxFitsPoolSubmit.");

static_assert(fpipe::CtxFitsPoolDispatchWithWorkload<eff::BgDrainCtx, decltype([](fpipe::WorkShard) {})>,
              "BgDrainCtx + shard-taking closure must satisfy "
              "CtxFitsPoolDispatchWithWorkload.");

// This context carries an empty row, so both gates must refuse it.
static_assert(!fpipe::CtxFitsPoolSubmit<eff::HotFgCtx, decltype([]() {})>,
              "HotFgCtx must not satisfy CtxFitsPoolSubmit.");

int main() {
    eff::HotFgCtx ctx;

    FakeConsumer<int> in_a, in_b;
    FakeProducer<int> out_a, out_b;

    auto stage_a = fpipe::mint_stage<&pass_through_a>(ctx, std::move(in_a), std::move(out_a));
    auto stage_b = fpipe::mint_stage<&pass_through_b>(ctx, std::move(in_b), std::move(out_b));

    // Only the composition is exercised here, not a pipeline run.
    auto pl = fpipe::mint_pipeline(ctx, std::move(stage_a), std::move(stage_b));
    (void)pl;

    // The background context here comes from the test scaffold.  A
    // production site takes one minted by the Keeper instead.
    eff::BgDrainCtx bg{};
    fpipe::Pool<> pool{fpipe::CoreCount{1}};

    std::atomic<std::size_t> hits{0};
    fpipe::pool_submit(bg, pool, [&hits]() noexcept { hits.fetch_add(1, std::memory_order_release); });

    auto result = fpipe::pool_dispatch_with_workload(
        bg, pool,
        fpipe::WorkloadProfile::from_budget(fpipe::WorkBudget{.read_bytes = 64, .write_bytes = 64, .item_count = 1},
                                            /*parallelism=*/1),
        [&hits]() noexcept { hits.fetch_add(1, std::memory_order_release); });
    (void)result;

    pool.wait_idle();

    if (hits.load(std::memory_order_acquire) < 1u) {
        std::fprintf(stderr,
                     "pool_submit and dispatch must increment hits "
                     "(observed %zu)\n",
                     hits.load(std::memory_order_acquire));
        std::abort();
    }

    return 0;
}
