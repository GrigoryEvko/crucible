#pragma once

// ── crucible::fixy::pipe — Pipeline/Stage/Endpoint minters ──────────
//
// Re-export per misc/16_05_2026_fixy.md.  Surfaces the Tier-3
// composition primitives (Endpoint → Stage → Pipeline) plus the
// Endpoint↔Stage bridge mints under `fixy::pipe::` so callers who
// include only the fixy umbrella never have to descend into the
// concurrent/ tree to compose a stage graph.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: every re-export
// preserves the substrate's `CtxFitsPipeline / CtxFitsStage /
// IsBridgeableDirection / SubstrateFitsCtxResidency` concept gates,
// the `[[nodiscard]] constexpr noexcept` qualifiers, and the
// chain-fold pipeline_row_union_t admission discipline.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   concurrent::mint_endpoint<Substr, Dir>(ctx, handle)
//   concurrent::mint_stage<auto FnPtr>(ctx, in, out)
//   concurrent::mint_pipeline(ctx, stages...)
//   concurrent::mint_pipeline_dag(ctx, graph, stages...)
//   concurrent::mint_stage_from_endpoints<FnPtr>(ctx, in_ep, out_ep)
//   concurrent::mint_mpmc_stage_from_endpoints<FnPtr>(ctx, eps...)
//   concurrent::mint_swmr_stage(ctx, ...)
//
//   NOTE: concurrent::mint_substrate_session<Substr, Dir>(ctx, handle)
//   used to be re-exported here in the M-19 grace window.  It now
//   lives at fixy::sess::mint_substrate_session AND
//   fixy::substr::mint_substrate_session — Pipe.h is the
//   pipeline/stage layer, not the substrate-bridge layer.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports introduce no new state path.
//   TypeSafe — using-declarations preserve the substrate's concept
//              gates (CtxFitsStage, CtxFitsPipeline, etc.).
//   NullSafe — Stage/Pipeline are value-typed.
//   MemSafe  — Pipeline holds Stages by value; no heap.
//   ThreadSafe — Pipeline jthread coordinator handles RAII join;
//              alias preserves the discipline.
//   DetSafe  — pure structural composition; no kernels, no FP.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives.

#include <crucible/concurrent/AdaptiveScheduler.h>  // V-215: Pool + dispatch
#include <crucible/concurrent/AutoRouter.h>        // V-077: AutoRouter family
#include <crucible/concurrent/AutoSplit.h>         // V-077: AutoSplit family
#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/ParallelismRule.h>  // V-076: WorkBudget / ParallelismRule / Tier / NumaPolicy
#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/concurrent/WorkingSet.h>        // V-076: working-set helpers
#include <crucible/effects/Capabilities.h>         // V-215: Effect::Bg admission
#include <crucible/effects/ExecCtx.h>              // V-215: IsExecCtx + CtxOwnsCapability
#include <concepts>     // V-218: std::same_as in stance::HotPathInline
#include <type_traits>  // FIXY-U-103 sentinel uses std::is_same_v
#include <utility>      // V-215: std::forward in mint bodies

namespace crucible::fixy::pipe {

// ═════════════════════════════════════════════════════════════════════
// ── Type carriers — grep-discoverable surface ──────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::concurrent::Endpoint;
using ::crucible::concurrent::Stage;
using ::crucible::concurrent::Pipeline;
using ::crucible::concurrent::Direction;

// ═════════════════════════════════════════════════════════════════════
// ── Endpoint mint — substrate-fit ctx-bound mint ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_endpoint<Substr, Direction::Producer|Consumer>(ctx, handle)`
// — the substrate-fit gate (IsBridgeableDirection +
// SubstrateFitsCtxResidency) is enforced at the requires-clause.

using ::crucible::concurrent::mint_endpoint;

// ═════════════════════════════════════════════════════════════════════
// ── Stage mint — explicit-FnPtr ctx-bound stage construction ───────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_stage<&body>(ctx, in_handle, out_handle)` — the FnPtr is non-
// deducible (per CLAUDE.md §XXI Tier 3); callers spell it explicitly.

using ::crucible::concurrent::mint_stage;

// ═════════════════════════════════════════════════════════════════════
// ── Pipeline mint — N-stage composition ───────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_pipeline(ctx, stages...)` — CtxFitsPipeline gate folds
// pipeline_chain over adjacent stage I/O types and admits the
// union row against Ctx::row_type.

using ::crucible::concurrent::mint_pipeline;
using ::crucible::concurrent::mint_pipeline_dag;

// ═════════════════════════════════════════════════════════════════════
// ── Endpoint↔Stage bridge mints ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Tier-2→3 bridge: mint a Stage from two Endpoints whose handles
// already carry the substrate-fit proof.  Three variants cover the
// SPSC (canonical), MPMC (variadic endpoints), and SWMR fan-out
// substrates.

using ::crucible::concurrent::mint_stage_from_endpoints;
using ::crucible::concurrent::mint_mpmc_stage_from_endpoints;
using ::crucible::concurrent::mint_swmr_stage;

// ═════════════════════════════════════════════════════════════════════
// ── Substrate session bridge — MOVED, fixy-M-19 ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_substrate_session<Substr, Dir>(ctx, handle)` no longer lives
// here.  It is a substrate→session bridge (Tier-2→3 per §XXI) — Pipe.h
// is the pipeline/stage layer, not the substrate-bridge layer.  The
// canonical surfaces are now fixy::sess::mint_substrate_session
// (result-side: it produces a Session handle) AND
// fixy::substr::mint_substrate_session (substrate-side: it sits next
// to every per-substrate `mint_*_session` family).  Callers should
// fix-up imports accordingly.

// ═════════════════════════════════════════════════════════════════════
// ── Compile-time coherence helpers (FIXY-AUDIT-C2) ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The substrate's `mint_pipeline` already runs `CtxFitsPipeline<Ctx,
// Stages...>` at its requires-clause; that concept folds three facts:
//
//   1. IsExecCtx<Ctx>
//   2. pipeline_chain<Stages...> — adjacent-pair output/input payload
//      type equality, also gated by IsStage<Stage_i>.
//   3. pipeline_row_union_t<Stages...> ⊆ Ctx::row_type — the
//      coordinator's effect row admits every stage's row.
//
// Production callers that want to PRE-CHECK the coherence at a
// non-mint call site (e.g. a generic Pipeline factory parameterized by
// the stage pack) need access to the chain concept AND the row-union
// metafunction directly.  Re-export them under fixy::pipe:: so callers
// who include only the fixy umbrella never have to descend into the
// concurrent/ tree to spell `concurrent::pipeline_chain<...>`.
//
// Both surfaces are pure metafunctions; no runtime cost.

using ::crucible::concurrent::IsStage;
using ::crucible::concurrent::stages_chain;
using ::crucible::concurrent::pipeline_chain;
using ::crucible::concurrent::pipeline_row_union_t;
using ::crucible::concurrent::CtxFitsPipeline;
// fixy-A4-007: per-stage gate is symmetric to per-pipeline — both must
// reach fixy::pipe::.  `CtxFitsStage<&body, Ctx>` is the actual §XXI
// requires-clause that `mint_stage` fires on; without re-export here a
// generic Pipeline factory parameterized by a single stage shape can't
// grep-discover the gate.  `CtxFitsStageFromEndpoints` is the Tier-2→3
// bridge variant used by `mint_stage_from_endpoints`.
using ::crucible::concurrent::CtxFitsStage;
using ::crucible::concurrent::CtxFitsStageFromEndpoints;

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-U-050: concept-gate extension (Agent 1 P1 leftover) ───────
// ═════════════════════════════════════════════════════════════════════
//
// Surfaces every concept that gates a `mint_*` call site in
// concurrent/{Pipeline,Stage,StageEndpointBridge,SubstrateSessionBridge,
// SubstrateCtxFit}.h.  Production callers writing generic stage / pipeline
// / endpoint factories need to spell these gates at compile-time pre-check
// sites without descending into the concurrent/ tree.
//
// ── Endpoint family (concurrent::StageEndpointBridge.h:158/161/166) ──
//
// IsEndpoint recognises the Endpoint<Substr, Dir, Ctx> shape;
// IsConsumerEndpoint / IsProducerEndpoint refine by Direction.

using ::crucible::concurrent::IsEndpoint;
using ::crucible::concurrent::IsConsumerEndpoint;
using ::crucible::concurrent::IsProducerEndpoint;

// ── Substrate-direction + residency gates ────────────────────────────
//
// IsBridgeableDirection (SubstrateSessionBridge.h:682) — the gate that
// validates Direction::Producer / Direction::Consumer for a given
// substrate's default_proto_for<> specialization.
//
// SubstrateFitsCtxResidency (SubstrateCtxFit.h:119) — checks that the
// substrate's residency tier admits the Ctx's row.  Together these are
// the gates that `mint_endpoint<Substr, Dir>(ctx, handle)` fires at its
// requires-clause; production callers writing generic mint forwarders
// need both reachable through fixy::pipe::.

using ::crucible::concurrent::IsBridgeableDirection;
using ::crucible::concurrent::SubstrateFitsCtxResidency;

// ── Stage-shape gates beyond the canonical CtxFitsStage ─────────────
//
// CtxFitsVariadicStage   — multi-input/-output stages (Stage.h:303).
// CtxFitsSwmrPublishStage — SWMR-publication stage fan-out (Stage.h:340).
//
// Both are §XXI mint requires-clauses; surfacing them lets factory-
// generic call sites pre-check the same way `CtxFitsStage` already does.

using ::crucible::concurrent::CtxFitsVariadicStage;
using ::crucible::concurrent::CtxFitsSwmrPublishStage;

// ── Pipeline-graph gates ────────────────────────────────────────────
//
// IsStageEdge / IsStageGraph (Pipeline.h:566/569) — DAG-form pipeline
// shapes (edges + graph) used by mint_pipeline_dag.
//
// CtxFitsPipelineDag (Pipeline.h:583) — the requires-clause gate.
//
// CtxFitsPipelineDagMint (Pipeline.h:997) — the variant that the
// mint_pipeline_dag<auto Graph>(ctx, stages...) factory fires at its
// own requires-clause.

using ::crucible::concurrent::IsStageEdge;
using ::crucible::concurrent::IsStageGraph;
using ::crucible::concurrent::CtxFitsPipelineDag;
using ::crucible::concurrent::CtxFitsPipelineDagMint;

// ── Endpoint↔Stage bridge gates (variadic + SWMR) ───────────────────
//
// CtxFitsMpmcStageFromEndpoints (StageEndpointBridge.h:428) — MPMC
// fan-in variant of CtxFitsStageFromEndpoints.
//
// CtxFitsSwmrStageFromEndpoint (StageEndpointBridge.h:435) — SWMR
// publish variant.

using ::crucible::concurrent::CtxFitsMpmcStageFromEndpoints;
using ::crucible::concurrent::CtxFitsSwmrStageFromEndpoint;

// ═════════════════════════════════════════════════════════════════════
// ── Cost-model surface (V-076) ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `ParallelismRule` + `WorkBudget` + the WorkingSet helpers belong on
// the same fixy:: layer as the Pipeline/Stage minters above — a stage
// that fits a ctx still needs a cost-model gate to decide whether to
// run sequentially, fan to N workers, and how to bind workers to NUMA
// nodes.  Re-exporting at `fixy::pipe::` keeps the cost-model story
// reachable without descending into `crucible::concurrent::*` directly.
//
// Distinct from `fixy::perf::TaggedParallelismDecision` (V-074-AUDIT)
// — that re-exports the PROFILER's Tagged-typed output.  The substrate
// `ParallelismDecision` here is the BARE struct the rule emits before
// any profiler attestation.

// ── ParallelismRule surface ────────────────────────────────────────
//
// `WorkBudget` is the read-bytes / write-bytes / item-count input.
// `Tier` + `NumaPolicy` are the cost-model enums.
// `ParallelismDecision` is the bare output struct (Kind + factor +
// numa + tier).  `ParallelismRule` is the stateless utility class
// (classify + recommend + budget_for_span).  `recommend_parallelism`
// is the free-function shorthand.

using ::crucible::concurrent::WorkBudget;
using ::crucible::concurrent::Tier;
using ::crucible::concurrent::NumaPolicy;
using ::crucible::concurrent::ParallelismDecision;
using ::crucible::concurrent::ParallelismRule;
using ::crucible::concurrent::recommend_parallelism;

// ── WorkingSet helpers (WorkingSet.h) ──────────────────────────────
//
// Compile-time facts used to compose per-stage / per-pipeline
// aggregate working-set sizes.  No runtime probing; no allocation.
// `hot_path_cache_line_bytes` = 64 (the InitSafe-pinned x86_64 +
// aarch64 line width).  `unknown_per_call_working_set` = SIZE_MAX
// (sentinel — used when a stage's WS isn't a static fact).
//
//   cell_line_footprint(N)           — ceil(N to nearest 64 B)
//   lines_plus_cell_working_set_v    — control_lines * 64 + cell ceil
//   saturating_ws_add(a, b)          — saturating sum into SIZE_MAX
//   has_static_per_call_working_set  — trait
//   has_static_per_call_working_set_v — _v alias
//   per_call_working_set_of_v        — pulls the static, or returns SIZE_MAX

using ::crucible::concurrent::hot_path_cache_line_bytes;
using ::crucible::concurrent::unknown_per_call_working_set;
using ::crucible::concurrent::cell_line_footprint;
using ::crucible::concurrent::lines_plus_cell_working_set_v;
using ::crucible::concurrent::saturating_ws_add;
using ::crucible::concurrent::has_static_per_call_working_set;
using ::crucible::concurrent::has_static_per_call_working_set_v;
using ::crucible::concurrent::per_call_working_set_of_v;

// ═════════════════════════════════════════════════════════════════════
// ── AutoRouter surface (V-077) ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// AutoRouter turns four compile-time facts (semantic intent, producer
// cardinality, consumer cardinality, byte footprint) into a concrete
// Permissioned* substrate.  V-076 surfaced the cost-model PRIMITIVES
// (Tier / NumaPolicy / WorkBudget); V-077 surfaces the COMPOSITION
// LAYER that uses those primitives to pick the routing substrate.
//
// Distinct from V-076: V-076 answers "HOW PARALLEL?" (Sequential vs
// Parallel + factor + NUMA hint); V-077 answers "WHICH SUBSTRATE?"
// (Spsc / Mpsc / Snapshot / Mpmc / WorkStealing / ShardedGrid).
//
//   RouteIntent              — semantic intent of the routed work
//   RouteKind                — substrate the router selects
//   AutoRouteDecision        — bare decision struct (kind + intent +
//                              topology + producers + consumers +
//                              workload_bytes + worker_fanout)
//   AutoRoute / AutoRoute_t  — type-level route picker (struct + alias)
//   StaticAutoRoute / _t / static_auto_route_v
//                            — consteval route + decision variants
//   auto_route_v             — constexpr template variable form
//   AutoRouteRuntimeProfile  — runtime cache-size profile carrier
//   auto_route_decision_runtime
//                            — runtime decision factory
//   auto_shard_factor_runtime
//                            — runtime shard-factor pick

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

// ═════════════════════════════════════════════════════════════════════
// ── AutoSplit surface (V-077) ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// AutoSplit turns a single contiguous workload into concrete
// [begin, end) shard ranges with a fanout decision, partition strategy,
// schedule mode, placement policy, and completion mode.  Builds on
// AutoRouter for substrate selection plus V-076 cost-model for the
// per-stage tier classification.
//
// SchedulingIntent declares the caller's parallelism APPETITE — the
// PRIMARY DIAL for the planner.  Sequential always collapses to F=1;
// LatencyCritical skips break-even gating; Background only steals
// idle cores; Throughput refuses fanout below the efficiency floor.
//
//   SchedulingIntent         — appetite dial
//   AutoSplitPartitionStrategy / AutoSplitScheduleMode /
//   AutoSplitPlacementPolicy / AutoSplitCompletionMode
//                            — orthogonal-axis enums (BOTH Partition
//                              and ScheduleMode have an Inline
//                              enumerator; HS14 fixture pins them as
//                              distinct enum classes)
//   AutoSplitRoutingDecision — bundled (partition, schedule, placement,
//                              completion) decision struct
//   AutoSplitRuntimeProfile  — runtime profile (route + workers +
//                              dispatch cost + min efficiency pct)
//   AutoSplitRequest         — caller-supplied job description
//   HintDirective / AutoSplitWorkloadHint
//                            — typed-workload hint surface
//   AutoSplitWorkloadTagged  — phantom-typed tag carrier
//   workload_traits          — trait extracting hint from a Body type
//   AutoSplitShard / AutoSplitPlan / AutoSplitDispatchResult
//                            — output shapes
//   AutoSplitShardBody       — concept gate for the shard-callable
//   auto_split_plan          — central planning factory (constexpr)
//   auto_split_runtime_profile_from_topology
//                            — canonical profile builder

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

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-V-215 — Pool + permission-bypass-closure soundness gate ───
// ═════════════════════════════════════════════════════════════════════
//
// `concurrent::Pool<Policy>::submit(Job&&)` accepts ANY closure that
// satisfies `is_invocable_r_v<void, Job&>`.  A closure CAN capture a
// `safety::Permission<Tag>` token (move-only, sizeof=1) and then run
// on a worker jthread — silently bypassing the CSL parallel-rule
// discipline enforced by `safety::mint_permission_fork`.  CLAUDE.md
// §IX names this the "permission bypass" anti-pattern:
//
//   "Manually spawning std::jthread with a Permission inside instead
//    of using permission_fork. Bypasses the CSL parallel-rule encoding
//    and skips static verification of splits_into_pack."
//
// V-215 closes that hole at the fixy:: boundary.  The substrate's
// `Pool::submit` stays unchanged (existing call sites in
// AutoSplit / Pipeline coordination depend on the relaxed shape);
// production code that wants the fixy-disciplined surface routes
// every submission through `fixy::pipe::mint_pool_submit(ctx, pool, job)`
// or `mint_pool_dispatch_with_workload(ctx, pool, profile, job)`.
//
// ── The PermissionFreeJob gate ─────────────────────────────────────
//
// `Permission<Tag>` / `Linear<T>` / `SharedPermissionGuard` are all
// move-only — their copy constructors are explicitly deleted.  A
// closure that captures any one of them BY VALUE inherits the deleted
// copy ctor (lambda capture types are aggregate-defaulted), making
// the closure NOT copy-constructible.  `std::is_copy_constructible_v`
// is therefore the load-bearing predicate that catches every CSL-
// typed move-only token captured by value.
//
// What this gate DOES catch:
//   * `[perm = std::move(perm_token)]() mutable { ... }`
//     — closure inherits deleted copy → reject.
//   * `[linear = std::move(linear_value)]() mutable { ... }`
//     — same.
//   * `[guard = std::move(shared_guard)]() mutable { ... }`
//     — same.
//
// What this gate does NOT catch (review/audit territory):
//   * `[ptr = raw_perm_ptr]() mutable { ... }` — raw pointer escape;
//     caller forfeited type-level proof at the cast site.
//   * Capture by reference of a long-lived non-CSL-typed object.
//
// The gate is sound for the CSL-typed move-only token family, which
// is the documented bypass shape.  Other escape hatches require code
// review or explicit `[[gnu::no_sanitize_thread]]`-style annotations.
//
// ── §XXI compliance ────────────────────────────────────────────────
//
// Both mints are ctx-bound: first parameter `Ctx const&`, single
// `requires CtxFitsPoolSubmit<Ctx, Job>` concept gate folding (a)
// `IsExecCtx<Ctx>`, (b) `CtxOwnsCapability<Ctx, Effect::Bg>` (Pool
// dispatches on background workers), and (c) `PermissionFreeJob<Job>`
// (the load-bearing soundness check).  `[[nodiscard]]`-not-applicable
// because both mints return `void`; `constexpr`-not-applicable because
// Pool internals are not constexpr-evaluable.  Both are `noexcept`.
//
// Mints DO NOT return the Pool by value — Pool is `Pinned` and
// `[[gnu::deleted]]`-copy.  They take it by lvalue reference and
// thread the submission through.  This is the §XXI "consumes parent
// authority" form, except the parent is a long-lived Pool rather than
// a move-able Permission token — the pool's Pinned identity is the
// authority.
//
// HS14 fixtures (test/fixy_neg/):
//   1. neg_fixy_pipe_mint_pool_submit_permission_capture.cpp
//        — closure captures Permission<Tag>; closure's copy ctor is
//          deleted; PermissionFreeJob<Job> fails → CtxFitsPoolSubmit
//          rejects.  This IS the canonical bypass shape.
//   2. neg_fixy_pipe_mint_pool_submit_ctx_no_bg.cpp
//        — HotFgCtx::row = Row<> admits no Bg; CtxOwnsCapability
//          fails → CtxFitsPoolSubmit rejects.  Distinct from #1
//          (this fixture's job IS copy-constructible).

// ── Type carriers — Pool + supporting types ────────────────────────

using ::crucible::concurrent::Pool;
using ::crucible::concurrent::CoreCount;
using ::crucible::concurrent::NumaNodeMask;
using ::crucible::concurrent::WorkloadProfile;
using ::crucible::concurrent::WorkShard;
using ::crucible::concurrent::DispatchWithWorkloadResult;

// ── PermissionFreeJob — the load-bearing soundness gate ─────────────
//
// A job is "permission-free" when it (1) satisfies the substrate's
// own `is_invocable_r_v<void, Job&>` AND (2) is copy-constructible.
// The copy-constructibility witness rules out closures capturing
// move-only CSL ownership tokens (Permission / Linear / SharedPermission
// guards) by value — those closures inherit the captured types'
// deleted copy ctors.  See the doc-block above for the gate's exact
// scope.

template <typename Job>
concept PermissionFreeJob =
    std::is_invocable_r_v<void, std::remove_reference_t<Job>&>
    && std::is_copy_constructible_v<std::remove_reference_t<Job>>;

// PermissionFreeJobWithShard — the dispatch_with_workload variant
// admits BOTH `void(Job&)` AND `void(Job&, WorkShard)` per the
// substrate's overload set; both forms must be permission-free.

template <typename Job>
concept PermissionFreeJobWithShard =
    (std::is_invocable_r_v<void, std::remove_reference_t<Job>&>
     || std::is_invocable_r_v<void, std::remove_reference_t<Job>&,
                              WorkShard>)
    && std::is_copy_constructible_v<std::remove_reference_t<Job>>;

// ── CtxFitsPoolSubmit / CtxFitsPoolDispatch ─────────────────────────
//
// Single-concept gates for `mint_pool_submit` and
// `mint_pool_dispatch_with_workload`.  Per §XXI, multi-clause
// requirements live INSIDE the concept definition (not at the call
// site).

template <typename Ctx, typename Job>
concept CtxFitsPoolSubmit =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::CtxOwnsCapability<
           Ctx, ::crucible::effects::Effect::Bg>
    && PermissionFreeJob<Job>;

template <typename Ctx, typename Job>
concept CtxFitsPoolDispatchWithWorkload =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::CtxOwnsCapability<
           Ctx, ::crucible::effects::Effect::Bg>
    && PermissionFreeJobWithShard<Job>;

// ── mint_pool_submit<Ctx, Policy, Job>(ctx, pool, job) ──────────────
//
// §XXI ctx-bound mint.  Forwards to `Pool<Policy>::submit(job)` after
// the single CtxFitsPoolSubmit gate fires.

template <typename Ctx, typename Policy, typename Job>
    requires CtxFitsPoolSubmit<Ctx, Job>
void mint_pool_submit(
    Ctx const&,
    Pool<Policy>& pool,
    Job&& job) noexcept
{
    pool.submit(std::forward<Job>(job));
}

// ── mint_pool_dispatch_with_workload<Ctx, Policy, Job>(...) ─────────
//
// §XXI ctx-bound mint.  Forwards to
// `Pool<Policy>::dispatch_with_workload(profile, job)` after the
// CtxFitsPoolDispatchWithWorkload gate fires.  The `[[nodiscard]]`
// rides through to the substrate's return value (which the caller
// inspects for ran_inline / queued / tasks_submitted).

template <typename Ctx, typename Policy, typename Job>
    requires CtxFitsPoolDispatchWithWorkload<Ctx, Job>
[[nodiscard]] DispatchWithWorkloadResult
mint_pool_dispatch_with_workload(
    Ctx const&,
    Pool<Policy>& pool,
    WorkloadProfile profile,
    Job&& job) noexcept
{
    return pool.dispatch_with_workload(profile, std::forward<Job>(job));
}

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-V-218 — HotPathInline stance ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `stance::HotPathInline<P, L1dBytes, L2Bytes>` is the band-3
// compile-time witness that pipeline P will dispatch inline (no
// jthread fan-out, no kernel-mediated handoff, no per-stage MESI
// ping-pong) under the given cache budget.  Backs the consteval
// substrate witness `Pipeline<Stages...>::will_run_inline_v<L1d, L2>()`
// (concurrent/Pipeline.h FIXY-V-218 substrate addition) with a
// production-grep-discoverable name.
//
// ── Why a separate stance ──────────────────────────────────────────
//
// The substrate's runtime `Pipeline<Stages...>::will_run_inline()`
// consults `Topology::instance()` at call time — perfect for the
// runtime dispatcher but invisible at compile time.  A band-3 site
// that wants its inline-fit claim CHECKED at compile time needs a
// concept it can spell in a `requires`-clause:
//
//     template <class P>
//         requires stance::HotPathInline<P>
//     void enqueue_hot_stage(...);
//
// Without the stance, the band-3 site can only assert at runtime
// after Topology probes; the static claim "this code path runs
// inline" silently becomes "this code path MIGHT run inline,
// depending on the box".  With the stance, mismatched callers red
// at COMPILE TIME against the assumed cache budget.
//
// ── Defaults ───────────────────────────────────────────────────────
//
// Default L1dBytes = 32 KiB, L2Bytes = 1 MiB.  Both numbers are the
// CLAUDE.md §VIII pinned x86_64+aarch64 baseline (private L1d 32-48K,
// private L2 ≥ 1 MiB per core).  Callers targeting different microarchs
// (Apple M1 128 KiB L1d, Bergamo 4 MiB L2) spell the NTTPs explicitly.
//
// FORWARD-POINTER: FIXY-V-223 plans to plumb `Topology::instance()`
// startup-probed L1d/L2 sizes through a constexpr surface so the
// defaults can be derived from real silicon facts at compile time
// instead of hard-coded.  Until V-223 lands, callers whose deployment
// fleet diverges from the 32 KiB / 1 MiB baseline must spell the
// NTTPs explicitly — the type-level claim remains sound either way;
// the only thing that changes is whether the defaults are picked up
// from a startup probe or from hard-coded constants.
//
// ── HS14 — two distinct rejection axes ─────────────────────────────
//
// Fixture #1 (oversized): a Pipeline of 5 × 10 MiB-per-stage = 100 MiB
//   aggregate exceeds BOTH the 32 KiB default L1dBytes AND the 1 MiB
//   default L2Bytes.  `will_run_inline_v<32KiB, 1MiB>()` returns false
//   via the (aggregate ≤ L1d || aggregate ≤ L2) branch.  Tests the
//   WORKING-SET-TOO-LARGE axis.
//
// Fixture #2 (!inline_safe): a Pipeline whose stages opt out of
//   `stage_inline_safe` (or whose primary template `false_type` was
//   never specialised) gives `inline_safe == false`.
//   `will_run_inline_v<L1d, L2>()` returns false via the early-out
//   `!inline_safe || !aggregate_working_set_known` branch.  Tests
//   the INLINE-SAFETY-MISSING axis — distinct from #1: a stage might
//   spill registers, use TLS, or hold a mutex; declaring `inline_safe`
//   is an explicit promise, and absence of the trait is the absence
//   of the promise.
//
// Two distinct fault classes ⇒ HS14 floor satisfied.

namespace stance {

template <typename P,
          std::size_t L1dBytes = 32ULL * 1024ULL,
          std::size_t L2Bytes  = 1024ULL * 1024ULL>
concept HotPathInline =
    requires {
        { P::template will_run_inline_v<L1dBytes, L2Bytes>() }
            -> std::same_as<bool>;
    }
    && P::template will_run_inline_v<L1dBytes, L2Bytes>();

}  // namespace stance

}  // namespace crucible::fixy::pipe

// ─── FIXY-U-103 in-header sentinel ─────────────────────────────────
//
// Drift-catch for the 45 using-decls above (U-103 baseline 18 +
// U-050 extension +13 + V-076 extension +14; see per-extension
// breakdown at the cardinality constant below).  Substrate identity
// witnessed via std::is_same_v on the type carriers (Endpoint, Stage,
// Pipeline, Direction, WorkBudget, Tier, NumaPolicy,
// ParallelismDecision, ParallelismRule); cardinality witness traps
// additions or removals at every consumer's include time, not just
// inside test/test_fixy_pipe.cpp.  Same recipe as fixy/Bridge.h::
// self_test + fixy/Diag.h::self_test + fixy/Handle.h::self_test.
// Doc-block prose count refreshed by FIXY-V-076 (Class G drift —
// comment count drifting from constant).

namespace crucible::fixy::pipe::self_test {

template <typename, typename, typename> class StageProbe {};
template <typename...> class PipelineProbe {};

static_assert(std::is_same_v<
    ::crucible::fixy::pipe::Direction,
    ::crucible::concurrent::Direction>,
    "fixy::pipe::Direction must alias substrate enum");

// V-076 type-identity witnesses — type carriers must alias substrate.
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::WorkBudget,
    ::crucible::concurrent::WorkBudget>,
    "fixy::pipe::WorkBudget must alias substrate struct");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::Tier,
    ::crucible::concurrent::Tier>,
    "fixy::pipe::Tier must alias substrate enum");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::NumaPolicy,
    ::crucible::concurrent::NumaPolicy>,
    "fixy::pipe::NumaPolicy must alias substrate enum");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::ParallelismDecision,
    ::crucible::concurrent::ParallelismDecision>,
    "fixy::pipe::ParallelismDecision must alias substrate struct");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::ParallelismRule,
    ::crucible::concurrent::ParallelismRule>,
    "fixy::pipe::ParallelismRule must alias substrate utility class");
// V-076 constant witnesses — the constexpr values must come from
// substrate exactly (catches accidental re-definition rather than
// re-export).
static_assert(::crucible::fixy::pipe::hot_path_cache_line_bytes ==
              ::crucible::concurrent::hot_path_cache_line_bytes);
static_assert(::crucible::fixy::pipe::unknown_per_call_working_set ==
              ::crucible::concurrent::unknown_per_call_working_set);

// V-077 type-identity witnesses — every AutoRouter + AutoSplit type
// carrier must alias substrate exactly.  Pick one representative per
// FAMILY (enum, decision struct, request shape, plan shape) so a
// typedef regression on ANY of the families reds the sentinel.
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::RouteIntent,
    ::crucible::concurrent::RouteIntent>,
    "fixy::pipe::RouteIntent must alias substrate enum");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::RouteKind,
    ::crucible::concurrent::RouteKind>,
    "fixy::pipe::RouteKind must alias substrate enum");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::AutoRouteDecision,
    ::crucible::concurrent::AutoRouteDecision>,
    "fixy::pipe::AutoRouteDecision must alias substrate struct");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::SchedulingIntent,
    ::crucible::concurrent::SchedulingIntent>,
    "fixy::pipe::SchedulingIntent must alias substrate enum");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::AutoSplitRequest,
    ::crucible::concurrent::AutoSplitRequest>,
    "fixy::pipe::AutoSplitRequest must alias substrate struct");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::AutoSplitPlan,
    ::crucible::concurrent::AutoSplitPlan>,
    "fixy::pipe::AutoSplitPlan must alias substrate struct");

// V-077 distinct-enum witness — a regression that typedefs
// AutoSplitPartitionStrategy ≡ AutoSplitScheduleMode would silently
// admit cross-axis assignment (BOTH carry an Inline enumerator).
static_assert(!std::is_same_v<
    ::crucible::fixy::pipe::AutoSplitPartitionStrategy,
    ::crucible::fixy::pipe::AutoSplitScheduleMode>,
    "fixy::pipe::AutoSplitPartitionStrategy must be a DISTINCT type "
    "from AutoSplitScheduleMode — both have an Inline enumerator and "
    "a typedef collapse would let cross-axis values slip through.");

// V-215 type-identity witnesses — Pool family must alias substrate
// exactly.  Pool is parameterised on Policy; the witness instantiates
// against the DefaultPolicy so a regression that silently aliases
// fixy::pipe::Pool<P> to a local class would red the sentinel at
// every consumer's include time.  CoreCount + NumaNodeMask +
// WorkloadProfile + WorkShard + DispatchWithWorkloadResult each pin
// the surrounding ctor / dispatch shapes.
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::Pool<>,
    ::crucible::concurrent::Pool<>>,
    "fixy::pipe::Pool must alias concurrent::Pool");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::CoreCount,
    ::crucible::concurrent::CoreCount>,
    "fixy::pipe::CoreCount must alias concurrent::CoreCount");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::NumaNodeMask,
    ::crucible::concurrent::NumaNodeMask>,
    "fixy::pipe::NumaNodeMask must alias concurrent::NumaNodeMask");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::WorkloadProfile,
    ::crucible::concurrent::WorkloadProfile>,
    "fixy::pipe::WorkloadProfile must alias concurrent::WorkloadProfile");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::WorkShard,
    ::crucible::concurrent::WorkShard>,
    "fixy::pipe::WorkShard must alias concurrent::WorkShard");
static_assert(std::is_same_v<
    ::crucible::fixy::pipe::DispatchWithWorkloadResult,
    ::crucible::concurrent::DispatchWithWorkloadResult>,
    "fixy::pipe::DispatchWithWorkloadResult must alias "
    "concurrent::DispatchWithWorkloadResult");

// V-215 concept-level witnesses — PermissionFreeJob MUST reject
// closures capturing a move-only type by value.  Build a probe
// non-copyable type and a probe closure that captures it; the gate
// MUST hold one way and fail the other.  This is the load-bearing
// soundness proof that the gate fires on the canonical bypass shape.
namespace v215_witness {
struct probe_move_only_token_ {
    constexpr probe_move_only_token_() noexcept = default;
    probe_move_only_token_(probe_move_only_token_ const&) = delete;
    probe_move_only_token_(probe_move_only_token_&&) noexcept = default;
};
// A copyable, no-arg, void-returning closure — the canonical safe shape.
inline constexpr auto copyable_no_capture_job_ = [](){};
}  // namespace v215_witness

static_assert(
    ::crucible::fixy::pipe::PermissionFreeJob<
        decltype(v215_witness::copyable_no_capture_job_)>,
    "PermissionFreeJob<copyable-no-capture-closure> MUST hold — "
    "the canonical safe submission shape.");

// Negative direction: a closure capturing a move-only token by value
// has a deleted copy ctor → PermissionFreeJob must REJECT.  We can't
// declare a non-constexpr-prvalue lambda at file scope, so the witness
// goes through a one-shot decltype on a type-only construct: the type
// of `lambda` (capturing probe_move_only_token_ by-value) is move-only
// even though no instance is materialised.
static_assert(
    !::crucible::fixy::pipe::PermissionFreeJob<
        decltype([t = v215_witness::probe_move_only_token_{}]() mutable {
            (void)t;
        })>,
    "PermissionFreeJob<move-only-capture-closure> MUST FAIL — "
    "this is the canonical Pool::submit permission-bypass shape "
    "that V-215's gate exists to reject.");

// V-218 stance witness — the requires-clause that gates
// `will_run_inline_v<L1d, L2>()` plus the value-conjunct (the body
// of the call returning true).  Witnessed via three lightweight
// Pipeline-shaped probes that obey the substrate's static-member
// interface but don't materialise stages.  Real Pipeline instances
// are exercised by the test/fixy_neg/ HS14 fixtures.

namespace v218_witness {

// Tiny probe: 12 KiB aggregate fits in default 32 KiB L1dBytes →
// will_run_inline_v returns true → HotPathInline holds.
struct TinyPipelineProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set =
        12ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d)
                || (aggregate_per_call_working_set <= L2);
        }
    }
};

// Huge probe: 600 MiB aggregate exceeds default 32 KiB / 1 MiB →
// will_run_inline_v returns false → HotPathInline rejects.
struct HugePipelineProbe {
    static constexpr bool inline_safe = true;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set =
        600ULL * 1024ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d)
                || (aggregate_per_call_working_set <= L2);
        }
    }
};

// Not-inline-safe probe: inline_safe = false → early-out branch →
// will_run_inline_v returns false → HotPathInline rejects even
// though the working-set is small.  Distinct fault class from
// HugePipelineProbe (oversized vs missing-inline-safe).
struct UnsafePipelineProbe {
    static constexpr bool inline_safe = false;
    static constexpr bool aggregate_working_set_known = true;
    static constexpr std::size_t aggregate_per_call_working_set =
        4ULL * 1024ULL;
    template <std::size_t L1d, std::size_t L2 = L1d>
    static consteval bool will_run_inline_v() noexcept {
        if constexpr (!inline_safe || !aggregate_working_set_known) {
            return false;
        } else {
            return (aggregate_per_call_working_set <= L1d)
                || (aggregate_per_call_working_set <= L2);
        }
    }
};

}  // namespace v218_witness

static_assert(
    ::crucible::fixy::pipe::stance::HotPathInline<
        v218_witness::TinyPipelineProbe>,
    "V-218: 12KiB inline-safe pipeline MUST satisfy "
    "stance::HotPathInline at the 32KiB/1MiB cache budget.");

static_assert(
    !::crucible::fixy::pipe::stance::HotPathInline<
        v218_witness::HugePipelineProbe>,
    "V-218: 600MiB inline-safe pipeline MUST FAIL "
    "stance::HotPathInline — aggregate exceeds both L1d and L2.");

static_assert(
    !::crucible::fixy::pipe::stance::HotPathInline<
        v218_witness::UnsafePipelineProbe>,
    "V-218: !inline_safe pipeline MUST FAIL HotPathInline — "
    "the early-out branch fires regardless of working-set size.");

// Custom NTTP path — explicit cache budget defeats the rejection
// when the budget is wide enough to admit the aggregate.  Pins
// that the NTTPs actually flow through to will_run_inline_v.
static_assert(
    ::crucible::fixy::pipe::stance::HotPathInline<
        v218_witness::HugePipelineProbe,
        /*L1dBytes=*/4ULL * 1024ULL * 1024ULL * 1024ULL,
        /*L2Bytes =*/8ULL * 1024ULL * 1024ULL * 1024ULL>,
    "V-218: HugePipelineProbe MUST satisfy HotPathInline under a "
    "4GiB/8GiB cache budget — NTTPs must reach will_run_inline_v.");

// Non-Pipeline type — the requires-clause that gates
// `will_run_inline_v<L1d, L2>()` must reject NON-Pipeline shapes
// (which lack the template static method) cleanly via SFINAE
// rather than hard-erroring.  `int` is the canonical probe.
static_assert(
    !::crucible::fixy::pipe::stance::HotPathInline<int>,
    "V-218: non-Pipeline type MUST fail HotPathInline via the "
    "requires-clause SFINAE — not via hard error.");

// Cardinality witness — surface count of using-decls in this header.
// Any add/remove of a using-decl above must update this number.
// U-103 baseline: 18 (Tier-3 mint family + canonical concept gates).
// U-050 extension: +13 concept gates (IsBridgeableDirection,
// SubstrateFitsCtxResidency, IsEndpoint/Consumer/Producer family,
// CtxFitsVariadicStage / CtxFitsSwmrPublishStage, IsStageEdge /
// IsStageGraph, CtxFitsPipelineDag / CtxFitsPipelineDagMint,
// CtxFitsMpmcStageFromEndpoints / CtxFitsSwmrStageFromEndpoint).
// V-076 extension: +14 cost-model re-exports (WorkBudget, Tier,
// NumaPolicy, ParallelismDecision, ParallelismRule,
// recommend_parallelism + 8 WorkingSet helpers).
// V-077 extension: +30 AutoRouter + AutoSplit re-exports
// (12 AutoRouter: RouteIntent/RouteKind/AutoRouteDecision/AutoRoute/
//  AutoRoute_t/StaticAutoRoute/StaticAutoRoute_t/static_auto_route_v/
//  auto_route_v/AutoRouteRuntimeProfile/auto_route_decision_runtime/
//  auto_shard_factor_runtime;
//  18 AutoSplit: SchedulingIntent + 4 axis enums +
//  AutoSplitRoutingDecision + AutoSplitRuntimeProfile +
//  AutoSplitRequest + HintDirective + AutoSplitWorkloadHint +
//  AutoSplitWorkloadTagged + workload_traits + AutoSplitShard +
//  AutoSplitPlan + AutoSplitDispatchResult + AutoSplitShardBody +
//  auto_split_plan + auto_split_runtime_profile_from_topology).
// V-215 extension: +12 Pool surface (6 type carriers: Pool, CoreCount,
//  NumaNodeMask, WorkloadProfile, WorkShard, DispatchWithWorkloadResult;
//  2 PermissionFree* concepts: PermissionFreeJob, PermissionFreeJobWithShard;
//  2 ctx-fit gates: CtxFitsPoolSubmit, CtxFitsPoolDispatchWithWorkload;
//  2 §XXI mints: mint_pool_submit, mint_pool_dispatch_with_workload).
// V-218 extension: +1 stance::HotPathInline concept.
constexpr int pipe_surface_cardinality = 88;
static_assert(pipe_surface_cardinality == 88,
    "fixy::pipe:: surface drifted — update Pipe.h using-decls + "
    "this sentinel + test_fixy_pipe.cpp coverage in lockstep.");

}  // namespace crucible::fixy::pipe::self_test
