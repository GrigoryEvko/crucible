#pragma once

// ── crucible::fixy — umbrella header ───────────────────────────────
//
// Single include for the entire fixy:: surface.  Pulling
// `<crucible/Fixy.h>` brings in every fixy header in stable
// dependency order so callers reach every minter, every grant tag,
// every dimension/lattice/reject diagnostic via one well-known
// include path.
//
// ── Phase A (foundation, shipped) ──────────────────────────────────
//   - fixy/Dim.h         — 20-axis DimensionAxis enum
//   - fixy/Default.h     — strict_default_for<D> per-axis defaults
//   - fixy/Grant.h       — grant::* engagement + relaxation tags
//   - fixy/Reject.h      — IsAccepted concept + FixyNotEngaged_<Axis>
//                          diagnostic tag tree
//   - fixy/Rules.h       — R001..R020 collision rule aliases
//
// ── Phase B (Fn aggregator, shipped) ───────────────────────────────
//   - fixy/Fn.h          — fn<Type, Grants...> wrapper + mint_fn +
//                          stance::* canonical bindings
//
// ── Phase C (substrate alias re-exports, shipped) ──────────────────
//   - fixy/Cap.h         — effects/Capability.h mint_cap / mint_from_ctx
//   - fixy/Perm.h        — permissions/* CSL token mints (root /
//                          split / combine / split_n / combine_n /
//                          share / fork / inherit)
//   - fixy/Sess.h        — sessions/* protocol combinators + mint
//                          factories + federation 3-role projection.
//                          Note: bare `mint_session<Proto>(ctx, res)`
//                          is `= delete`d; production code calls
//                          `mint_permissioned_session<Proto>(ctx, res,
//                          perms...)` (the empty-PermSet shim covers
//                          the zero-permission case).  The deleted
//                          decl is re-exported so stale call sites
//                          surface the canonical diagnostic via the
//                          `fixy::` path.
//   - fixy/Pipe.h        — concurrent/* Tier-3 composition
//                          (mint_endpoint / mint_stage / mint_pipeline /
//                          mint_stage_from_endpoints / mint_substrate_session)
//   - fixy/Bridge.h      — bridges/* wrap factories
//                          (mint_recording_session, mint_crash_watched_session,
//                          mint_persisted_session, endpoint variants,
//                          mint_vigil_mode_bridge)
//   - fixy/Substr.h      — per-substrate session mints
//                          (SPSC / SWMR / ChaseLev / MetaLog / ChainEdge /
//                          MPMC / CalendarGrid / ShardedCalendarGrid /
//                          ShardedGrid)
//   - fixy/Mach.h        — safety/Machine.h mint_machine + transition_to
//   - fixy/Safety.h      — safety/* token mints (Linear / Secret /
//                          ScopedView)
//   - fixy/Wrap.h        — value-level safety wrappers (Refined /
//                          SealedRefined / Tagged / Monotonic /
//                          AppendOnly / Stale / TimeOrdered /
//                          WriteOnce / WriteOnceNonNull /
//                          BoundedMonotonic / OrderedAppendOnly /
//                          AtomicMonotonic — plus Linear / Secret /
//                          SharedPermission re-exports for one-stop
//                          `fixy::wrap::` value-wrapping access)
//   - fixy/Struct.h      — structural (non-Graded) safety wrappers
//                          (Pinned / NonMovable / NotInherited /
//                          FinalBy / Checked.h primitives /
//                          ConstantTime / Simd facade / OwnedRegion /
//                          Workload parallel_for_views family)
//   - fixy/Is.h          — concept-gate aliases for safety/Is*.h
//                          (IsLinear / IsRefined / IsTagged / IsSecret /
//                          IsMonotonic-equivalent recognizers — 32 of
//                          them — plus IsWitness from
//                          safety/witness/IsWitness.h)
//
// ── Phase D (foundation universe re-exports, shipped) ─────────────
//   - fixy/Algebra.h     — algebra/Graded.h substrate + 30 lattices
//                          + GradedWrapper concept + Modality enum +
//                          law-verifier helpers
//   - fixy/Eff.h         — effects/* Met(X) surface: Row<>,
//                          Computation<>, Subrow, row_union_t /
//                          difference_t / intersection_t, F*
//                          aliases (Pure/Tot/Ghost/Div/ST/All),
//                          Capability<>, ExecCtx + 5 canonical ctxs,
//                          21+ Resource budget tags, ConcurrentRow,
//                          OsUniverse, EffectMask
//   - fixy/Diag.h        — safety/Diagnostic.h Category + Catalog +
//                          tag_of_t / category_of_v bijection + 28
//                          tag classes + stable_name / type_id /
//                          function_id + canonicalize_pack +
//                          insight_provider + row_hash_contribution
//   - fixy/Source.h      — safety/Tagged.h source::* / trust::* /
//                          access::* / version::* / vessel_trust::*
//                          phantom tag namespaces + safety/Secret.h
//                          secret_policy::* + Types.h hash_family::*
//
// ── Macro definition ───────────────────────────────────────────────
//
// Defines CRUCIBLE_FIXY=1 so downstream CMake targets can gate on
// the fixy surface availability.  Single-include discipline: include
// this umbrella ONCE per TU; individual fixy/*.h headers remain
// independently includable for targeted dependencies.

#define CRUCIBLE_FIXY 1

// ── Phase A — foundation ──────────────────────────────────────────
#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Rules.h>

// ── Phase B — Fn aggregator ───────────────────────────────────────
#include <crucible/fixy/Fn.h>

// ── Phase C — substrate alias re-exports ──────────────────────────
#include <crucible/fixy/Bridge.h>
#include <crucible/fixy/Cap.h>
#include <crucible/fixy/Is.h>
#include <crucible/fixy/Mach.h>
#include <crucible/fixy/Perm.h>
#include <crucible/fixy/Pipe.h>
#include <crucible/fixy/Safety.h>
#include <crucible/fixy/Sess.h>
#include <crucible/fixy/Struct.h>
#include <crucible/fixy/Substr.h>
#include <crucible/fixy/Wrap.h>

// ── Phase D — foundation universe re-exports ─────────────────────
#include <crucible/fixy/Algebra.h>
#include <crucible/fixy/Diag.h>
#include <crucible/fixy/Eff.h>
#include <crucible/fixy/Insights.h>
#include <crucible/fixy/Source.h>
