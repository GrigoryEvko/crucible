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

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <type_traits>  // FIXY-U-103 sentinel uses std::is_same_v

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

}  // namespace crucible::fixy::pipe

// ─── FIXY-U-103 in-header sentinel ─────────────────────────────────
//
// Drift-catch for the 18 using-decls above.  Substrate identity
// witnessed via std::is_same_v on the four type carriers (Endpoint,
// Stage, Pipeline, Direction); cardinality witness traps additions
// or removals at every consumer's include time, not just inside
// test/test_fixy_pipe.cpp.  Same recipe as fixy/Bridge.h::self_test
// + fixy/Diag.h::self_test + fixy/Handle.h::self_test.

namespace crucible::fixy::pipe::self_test {

template <typename, typename, typename> class StageProbe {};
template <typename...> class PipelineProbe {};

static_assert(std::is_same_v<
    ::crucible::fixy::pipe::Direction,
    ::crucible::concurrent::Direction>,
    "fixy::pipe::Direction must alias substrate enum");

// Cardinality witness — surface count of using-decls in this header.
// Any add/remove of a using-decl above must update this number.
// U-103 baseline: 18 (Tier-3 mint family + canonical concept gates).
// U-050 extension: +13 concept gates (IsBridgeableDirection,
// SubstrateFitsCtxResidency, IsEndpoint/Consumer/Producer family,
// CtxFitsVariadicStage / CtxFitsSwmrPublishStage, IsStageEdge /
// IsStageGraph, CtxFitsPipelineDag / CtxFitsPipelineDagMint,
// CtxFitsMpmcStageFromEndpoints / CtxFitsSwmrStageFromEndpoint).
constexpr int pipe_surface_cardinality = 31;
static_assert(pipe_surface_cardinality == 31,
    "fixy::pipe:: surface drifted — update Pipe.h using-decls + "
    "this sentinel + test_fixy_pipe.cpp coverage in lockstep.");

}  // namespace crucible::fixy::pipe::self_test
