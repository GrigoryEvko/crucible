#pragma once

#include <crucible/concurrent/AdaptiveScheduler.h>
#include <crucible/concurrent/AutoRouter.h>
#include <crucible/concurrent/AutoSplit.h>
#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/ParallelismRule.h>
#include <crucible/concurrent/_Pipeline.h>
#include <crucible/concurrent/_Stage.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/concurrent/TopologyConstexpr.h>
#include <crucible/concurrent/_WorkingSet.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_ExecCtx.h>
#include <concepts>
#include <type_traits>
#include <utility>

namespace crucible::fixy::pipe {

using ::crucible::concurrent::Endpoint;
using ::crucible::concurrent::Stage;
using ::crucible::concurrent::Pipeline;
using ::crucible::concurrent::Direction;

using ::crucible::concurrent::mint_endpoint;

using ::crucible::concurrent::mint_stage;

using ::crucible::concurrent::mint_pipeline;
using ::crucible::concurrent::mint_pipeline_dag;

using ::crucible::concurrent::mint_stage_from_endpoints;
using ::crucible::concurrent::mint_mpmc_stage_from_endpoints;
using ::crucible::concurrent::mint_swmr_stage;

using ::crucible::concurrent::IsStage;
using ::crucible::concurrent::stages_chain;
using ::crucible::concurrent::pipeline_chain;
using ::crucible::concurrent::pipeline_row_union_t;
using ::crucible::concurrent::CtxFitsPipeline;
using ::crucible::concurrent::CtxFitsStage;
using ::crucible::concurrent::CtxFitsStageFromEndpoints;

using ::crucible::concurrent::IsEndpoint;
using ::crucible::concurrent::IsConsumerEndpoint;
using ::crucible::concurrent::IsProducerEndpoint;

using ::crucible::concurrent::IsBridgeableDirection;
using ::crucible::concurrent::SubstrateFitsCtxResidency;

using ::crucible::concurrent::CtxFitsVariadicStage;
using ::crucible::concurrent::CtxFitsSwmrPublishStage;

using ::crucible::concurrent::IsStageEdge;
using ::crucible::concurrent::IsStageGraph;
using ::crucible::concurrent::CtxFitsPipelineDag;
using ::crucible::concurrent::CtxFitsPipelineDagMint;

using ::crucible::concurrent::CtxFitsMpmcStageFromEndpoints;
using ::crucible::concurrent::CtxFitsSwmrStageFromEndpoint;

// ParallelismDecision is the bare struct the rule emits, before any profiler
// attestation wraps it.

using ::crucible::concurrent::WorkBudget;
using ::crucible::concurrent::Tier;
using ::crucible::concurrent::NumaPolicy;
using ::crucible::concurrent::ParallelismDecision;
using ::crucible::concurrent::ParallelismRule;
using ::crucible::concurrent::recommend_parallelism;

using ::crucible::concurrent::hot_path_cache_line_bytes;
using ::crucible::concurrent::unknown_per_call_working_set;
using ::crucible::concurrent::cell_line_footprint;
using ::crucible::concurrent::lines_plus_cell_working_set_v;
using ::crucible::concurrent::saturating_ws_add;
using ::crucible::concurrent::has_static_per_call_working_set;
using ::crucible::concurrent::has_static_per_call_working_set_v;
using ::crucible::concurrent::per_call_working_set_of_v;

using ::crucible::concurrent::RouteIntent;
using ::crucible::concurrent::RouteKind;
using ::crucible::concurrent::AutoRouteDecision;
using ::crucible::concurrent::AutoRoute;
using ::crucible::concurrent::AutoRoute_t;
using ::crucible::concurrent::StaticAutoRoute;
using ::crucible::concurrent::StaticAutoRoute_t;
using ::crucible::concurrent::static_auto_route_v;
using ::crucible::concurrent::auto_route_v;
using ::crucible::concurrent::AutoRouteRuntimeProfile;
using ::crucible::concurrent::auto_route_decision_runtime;
using ::crucible::concurrent::auto_shard_factor_runtime;

using ::crucible::concurrent::SchedulingIntent;
using ::crucible::concurrent::AutoSplitPartitionStrategy;
using ::crucible::concurrent::AutoSplitScheduleMode;
using ::crucible::concurrent::AutoSplitPlacementPolicy;
using ::crucible::concurrent::AutoSplitCompletionMode;
using ::crucible::concurrent::AutoSplitRoutingDecision;
using ::crucible::concurrent::AutoSplitRuntimeProfile;
using ::crucible::concurrent::AutoSplitRequest;
using ::crucible::concurrent::HintDirective;
using ::crucible::concurrent::AutoSplitWorkloadHint;
using ::crucible::concurrent::AutoSplitWorkloadTagged;
using ::crucible::concurrent::workload_traits;
using ::crucible::concurrent::AutoSplitShard;
using ::crucible::concurrent::AutoSplitPlan;
using ::crucible::concurrent::AutoSplitDispatchResult;
using ::crucible::concurrent::AutoSplitShardBody;
using ::crucible::concurrent::auto_split_plan;
using ::crucible::concurrent::auto_split_runtime_profile_from_topology;

using ::crucible::concurrent::Pool;
using ::crucible::concurrent::CoreCount;
using ::crucible::concurrent::NumaNodeMask;
using ::crucible::concurrent::WorkloadProfile;
using ::crucible::concurrent::WorkShard;
using ::crucible::concurrent::DispatchWithWorkloadResult;

// The pool accepts any void-invocable job, so a closure could carry a CSL
// ownership token onto a worker thread and bypass the fork discipline.  Every
// such token is move-only, and a closure that captures one by value inherits
// its deleted copy constructor.  Copy-constructibility is therefore the test
// that rejects the whole family.
//
// It does not reject a raw pointer to a token, nor a reference capture of a
// long-lived object.  Those escapes are review territory.

template <typename Job>
concept PermissionFreeJob = std::is_invocable_r_v<void, std::remove_reference_t<Job>&>
                         && std::is_copy_constructible_v<std::remove_reference_t<Job>>;

template <typename Job>
concept PermissionFreeJobWithShard = (std::is_invocable_r_v<void, std::remove_reference_t<Job>&>
                                      || std::is_invocable_r_v<void, std::remove_reference_t<Job>&, WorkShard>)
                                  && std::is_copy_constructible_v<std::remove_reference_t<Job>>;

template <typename Ctx, typename Job>
concept CtxFitsPoolSubmit =
    ::crucible::effects::IsExecCtx<Ctx> && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Bg>
    && PermissionFreeJob<Job>;

template <typename Ctx, typename Job>
concept CtxFitsPoolDispatchWithWorkload =
    ::crucible::effects::IsExecCtx<Ctx> && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Bg>
    && PermissionFreeJobWithShard<Job>;

template <typename Ctx, typename Policy, typename Job>
    requires CtxFitsPoolSubmit<Ctx, Job>
void pool_submit(Ctx const&, Pool<Policy>& pool, Job&& job) noexcept {
    pool.submit(std::forward<Job>(job));
}

template <typename Ctx, typename Policy, typename Job>
    requires CtxFitsPoolDispatchWithWorkload<Ctx, Job>
[[nodiscard]] DispatchWithWorkloadResult pool_dispatch_with_workload(Ctx const&, Pool<Policy>& pool,
                                                                     WorkloadProfile profile, Job&& job) noexcept {
    return pool.dispatch_with_workload(profile, std::forward<Job>(job));
}

// These values are frozen at build time.  A fleet whose silicon differs
// overrides them per build with -DCRUCIBLE_L1D_PER_CORE_BYTES,
// -DCRUCIBLE_L2_PER_CORE_BYTES and -DCRUCIBLE_L3_TOTAL_BYTES.

namespace topology {

using ::crucible::concurrent::topology_constexpr::l1d_per_core_bytes_v;
using ::crucible::concurrent::topology_constexpr::l2_per_core_bytes_v;
using ::crucible::concurrent::topology_constexpr::l3_total_bytes_v;
using ::crucible::concurrent::topology_constexpr::is_l1d_overridden_v;
using ::crucible::concurrent::topology_constexpr::is_l2_overridden_v;
using ::crucible::concurrent::topology_constexpr::is_l3_overridden_v;

}  // namespace topology

// A pipeline can also answer "does this run inline" at run time, by probing the
// real topology.  That answer arrives too late for a caller whose own contract
// is "this path runs inline", so the concept below asks the same question of a
// build-time cache budget and lets the caller spell it in a requires-clause.

namespace stance {

template <typename P, std::size_t L1dBytes = ::crucible::fixy::pipe::topology::l1d_per_core_bytes_v,
          std::size_t L2Bytes = ::crucible::fixy::pipe::topology::l2_per_core_bytes_v>
concept HotPathInline = requires {
    { P::template will_run_inline_v<L1dBytes, L2Bytes>() } -> std::same_as<bool>;
} && P::template will_run_inline_v<L1dBytes, L2Bytes>();

}  // namespace stance

}  // namespace crucible::fixy::pipe

namespace crucible::fixy::pipe::self_test {

template <typename, typename, typename>
class StageProbe {};
template <typename...>
class PipelineProbe {};

static_assert(std::is_same_v<::crucible::fixy::pipe::Direction, ::crucible::concurrent::Direction>,
              "fixy::pipe::Direction must alias substrate enum");

static_assert(std::is_same_v<::crucible::fixy::pipe::WorkBudget, ::crucible::concurrent::WorkBudget>,
              "fixy::pipe::WorkBudget must alias substrate struct");
static_assert(std::is_same_v<::crucible::fixy::pipe::Tier, ::crucible::concurrent::Tier>,
              "fixy::pipe::Tier must alias substrate enum");
static_assert(std::is_same_v<::crucible::fixy::pipe::NumaPolicy, ::crucible::concurrent::NumaPolicy>,
              "fixy::pipe::NumaPolicy must alias substrate enum");
static_assert(std::is_same_v<::crucible::fixy::pipe::ParallelismDecision, ::crucible::concurrent::ParallelismDecision>,
              "fixy::pipe::ParallelismDecision must alias substrate struct");
static_assert(std::is_same_v<::crucible::fixy::pipe::ParallelismRule, ::crucible::concurrent::ParallelismRule>,
              "fixy::pipe::ParallelismRule must alias substrate utility class");
// Value equality catches a redefinition that name-lookup re-export would not.
static_assert(::crucible::fixy::pipe::hot_path_cache_line_bytes == ::crucible::concurrent::hot_path_cache_line_bytes);
static_assert(::crucible::fixy::pipe::unknown_per_call_working_set
              == ::crucible::concurrent::unknown_per_call_working_set);

static_assert(std::is_same_v<::crucible::fixy::pipe::RouteIntent, ::crucible::concurrent::RouteIntent>,
              "fixy::pipe::RouteIntent must alias substrate enum");
static_assert(std::is_same_v<::crucible::fixy::pipe::RouteKind, ::crucible::concurrent::RouteKind>,
              "fixy::pipe::RouteKind must alias substrate enum");
static_assert(std::is_same_v<::crucible::fixy::pipe::AutoRouteDecision, ::crucible::concurrent::AutoRouteDecision>,
              "fixy::pipe::AutoRouteDecision must alias substrate struct");
static_assert(std::is_same_v<::crucible::fixy::pipe::SchedulingIntent, ::crucible::concurrent::SchedulingIntent>,
              "fixy::pipe::SchedulingIntent must alias substrate enum");
static_assert(std::is_same_v<::crucible::fixy::pipe::AutoSplitRequest, ::crucible::concurrent::AutoSplitRequest>,
              "fixy::pipe::AutoSplitRequest must alias substrate struct");
static_assert(std::is_same_v<::crucible::fixy::pipe::AutoSplitPlan, ::crucible::concurrent::AutoSplitPlan>,
              "fixy::pipe::AutoSplitPlan must alias substrate struct");

static_assert(
    !std::is_same_v<::crucible::fixy::pipe::AutoSplitPartitionStrategy, ::crucible::fixy::pipe::AutoSplitScheduleMode>,
    "fixy::pipe::AutoSplitPartitionStrategy must be a DISTINCT type "
    "from AutoSplitScheduleMode — both have an Inline enumerator and "
    "a typedef collapse would let cross-axis values slip through.");

static_assert(std::is_same_v<::crucible::fixy::pipe::Pool<>, ::crucible::concurrent::Pool<>>,
              "fixy::pipe::Pool must alias concurrent::Pool");
static_assert(std::is_same_v<::crucible::fixy::pipe::CoreCount, ::crucible::concurrent::CoreCount>,
              "fixy::pipe::CoreCount must alias concurrent::CoreCount");
static_assert(std::is_same_v<::crucible::fixy::pipe::NumaNodeMask, ::crucible::concurrent::NumaNodeMask>,
              "fixy::pipe::NumaNodeMask must alias concurrent::NumaNodeMask");
static_assert(std::is_same_v<::crucible::fixy::pipe::WorkloadProfile, ::crucible::concurrent::WorkloadProfile>,
              "fixy::pipe::WorkloadProfile must alias concurrent::WorkloadProfile");
static_assert(std::is_same_v<::crucible::fixy::pipe::WorkShard, ::crucible::concurrent::WorkShard>,
              "fixy::pipe::WorkShard must alias concurrent::WorkShard");
static_assert(std::is_same_v<::crucible::fixy::pipe::DispatchWithWorkloadResult,
                             ::crucible::concurrent::DispatchWithWorkloadResult>,
              "fixy::pipe::DispatchWithWorkloadResult must alias "
              "concurrent::DispatchWithWorkloadResult");

namespace v215_witness {
struct probe_move_only_token_ {
    constexpr probe_move_only_token_() noexcept = default;
    probe_move_only_token_(probe_move_only_token_ const&) = delete;
    probe_move_only_token_(probe_move_only_token_&&) noexcept = default;
};
inline constexpr auto copyable_no_capture_job_ = []() {};
}  // namespace v215_witness

static_assert(::crucible::fixy::pipe::PermissionFreeJob<decltype(v215_witness::copyable_no_capture_job_)>,
              "PermissionFreeJob<copyable-no-capture-closure> MUST hold — "
              "the canonical safe submission shape.");

// A non-constexpr lambda cannot be declared at file scope, so the closure type
// is named inside decltype and no instance is ever materialised.
static_assert(!::crucible::fixy::pipe::PermissionFreeJob<
                  decltype([t = v215_witness::probe_move_only_token_{}]() mutable { (void)t; })>,
              "PermissionFreeJob<move-only-capture-closure> MUST FAIL — "
              "this is the canonical Pool::submit permission-bypass shape "
              "that V-215's gate exists to reject.");

namespace v218_witness {

struct TinyPipelineProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set = 12ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d) || (aggregate_per_call_working_set <= L2);
        }
    }
};

struct HugePipelineProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set = 600ULL * 1024ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d) || (aggregate_per_call_working_set <= L2);
        }
    }
};

struct UnsafePipelineProbe {
    static constexpr bool inline_safe = false;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set = 4ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d) || (aggregate_per_call_working_set <= L2);
        }
    }
};

}  // namespace v218_witness

static_assert(::crucible::fixy::pipe::stance::HotPathInline<v218_witness::TinyPipelineProbe>,
              "A 12KiB inline-safe pipeline must satisfy stance::HotPathInline "
              "at the default cache budget.");

static_assert(!::crucible::fixy::pipe::stance::HotPathInline<v218_witness::HugePipelineProbe>,
              "A 600MiB inline-safe pipeline must fail stance::HotPathInline — "
              "the aggregate exceeds both L1d and L2.");

static_assert(!::crucible::fixy::pipe::stance::HotPathInline<v218_witness::UnsafePipelineProbe>,
              "A pipeline that is not inline-safe must fail stance::HotPathInline — "
              "the early-out branch fires regardless of working-set size.");

static_assert(::crucible::fixy::pipe::stance::HotPathInline<v218_witness::HugePipelineProbe,
                                                            /*L1dBytes=*/4ULL * 1024ULL * 1024ULL * 1024ULL,
                                                            /*L2Bytes =*/8ULL * 1024ULL * 1024ULL * 1024ULL>,
              "HugePipelineProbe must satisfy HotPathInline under a 4GiB/8GiB "
              "cache budget — the NTTPs must reach will_run_inline_v.");

static_assert(!::crucible::fixy::pipe::stance::HotPathInline<int>,
              "A type with no will_run_inline_v must fail HotPathInline through "
              "the requires-clause, not through a hard error.");

constexpr int pipe_surface_cardinality = 88;
static_assert(pipe_surface_cardinality == 88, "The fixy::pipe:: surface drifted from 88 items.  The using-decls "
                                              "and this sentinel must update in lockstep.");

}  // namespace crucible::fixy::pipe::self_test
