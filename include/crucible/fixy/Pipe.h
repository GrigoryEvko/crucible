#pragma once

// ── crucible::fixy::pipe — Pipeline/Stage/Endpoint minters ──────────
//
// Phase C re-export per misc/16_05_2026_fixy.md.  Surfaces the Tier-3
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
//   concurrent::mint_substrate_session<Substr, Dir>(ctx, handle)
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
// ── Substrate session bridge ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// `mint_substrate_session<Substr, Dir>(ctx, handle)` — bridges every
// Permissioned* substrate (SPSC/MPSC/MPMC/ChaseLev/Sharded/
// CalendarGrid/MetaLog/ChainEdge) to a typed Session handle.

using ::crucible::concurrent::mint_substrate_session;

}  // namespace crucible::fixy::pipe
