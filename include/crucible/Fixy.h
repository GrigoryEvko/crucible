#pragma once

// ── crucible::fixy — umbrella header ───────────────────────────────
//
// Single include for the entire fixy:: surface.  Pulling
// `<crucible/Fixy.h>` brings in every fixy header in stable
// dependency order so callers reach every minter, every grant tag,
// every dimension/lattice/reject diagnostic via one well-known
// include path.
//
// ── Ordering convention ────────────────────────────────────────────
//
// Within each phase, both the doc-block below AND the #include block
// at the bottom of this header list entries ALPHABETICALLY (with one
// deliberate exception in Phase A — Reject.h ships before Profile.h
// because Profile.h's IsAcceptedActive instantiates the Reject.h
// IsAccepted concept; the dependency-order swap is intentional and
// header-functionally inert thanks to `#pragma once`).  Adding a new
// fixy/*.h header MUST update BOTH lists in lockstep — drift between
// doc-block and include-block is the fixy-A4-032 audit gap that this
// convention closes.
//
// ── Phase A (foundation, shipped) ──────────────────────────────────
//   - fixy/Default.h     — strict_default_for<D> per-axis defaults
//   - fixy/Dim.h         — 20-axis DimensionAxis enum
//   - fixy/Grant.h       — grant::* engagement + relaxation tags
//   - fixy/Reject.h      — IsAccepted concept + FixyNotEngaged_<Axis>
//                          diagnostic tag tree
//   - fixy/Profile.h     — IsAcceptedActive + fixy_is_strict toggle
//                          (CRUCIBLE_FIXY_STRICT — strict vs sketch
//                          mode selector; routes fixy::fn through
//                          IsAccepted (strict) or IsAcceptedSketch
//                          (always-true, hot-loop migration mode))
//   - fixy/Rules.h       — R001..R020 collision rule aliases
//
// ── Phase B (Fn aggregator, shipped) ───────────────────────────────
//   - fixy/Fn.h          — fn<Type, Grants...> wrapper + mint_fn +
//                          stance::* canonical bindings
//
// ── Phase C (substrate alias re-exports, shipped) ──────────────────
//   - fixy/Bridge.h      — bridges/* wrap factories
//                          (mint_recording_session, mint_crash_watched_session,
//                          mint_persisted_session, endpoint variants,
//                          mint_vigil_mode_bridge)
//   - fixy/Cap.h         — effects/Capability.h mint_cap / mint_from_ctx
//   - fixy/Contract.h    — safety/Contract.h CRUCIBLE_PRE/POST macros
//                          + Cipher tier-migration mints under
//                          fixy::contract::cipher::* (mint_promote,
//                          mint_demote, mint_restore, EpochedDelegate,
//                          mint_persisted_session)
//   - fixy/Decide.h      — safety/Decide.h named VC predicates under
//                          fixy::decide::* (23 predicates + Interval<T> —
//                          no_overflow_mul / no_overflow_sum / all_in_range /
//                          strictly_increasing / is_power_of_two_le /
//                          intervals_pairwise_disjoint / tier_replaces /
//                          row_subset / fmix_preserves_non_zero / ...)
//   - fixy/Is.h          — concept-gate aliases for safety/Is*.h
//                          (IsLinear / IsRefined / IsTagged / IsSecret /
//                          IsMonotonic-equivalent recognizers — 32 of
//                          them — plus IsWitness from
//                          safety/witness/IsWitness.h)
//   - fixy/Mach.h        — safety/Machine.h mint_machine + transition_to
//   - fixy/Perm.h        — permissions/* CSL token mints (root /
//                          split / combine / split_n / combine_n /
//                          share / fork / inherit)
//   - fixy/Pipe.h        — concurrent/* Tier-3 composition
//                          (mint_endpoint / mint_stage / mint_pipeline /
//                          mint_stage_from_endpoints / mint_substrate_session)
//   - fixy/Safety.h      — safety/* token mints (Linear / Secret /
//                          ScopedView)
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
//   - fixy/SessCT.h      — sessions/SessionCT.h CT-required payload
//                          chokepoint surfaced under
//                          fixy::sess::ct::* (CTPayload<T> + ct::eq
//                          overload + requires_ct trait family +
//                          is_ct_payload shape predicates +
//                          ct_payload_value_type metafn)
//   - fixy/SessContentAddr.h — sessions/SessionContentAddressed.h
//                          content-hash-quotient surface under
//                          fixy::sess::contentaddr::* (ContentAddressed<T>
//                          + is_content_addressed trait family +
//                          underlying/unwrap metafns + depth counter) —
//                          used by Cipher.h federation entry payload +
//                          cold-blob region persistence types (FIXY-U-052c)
//   - fixy/SessEventLog.h — sessions/SessionEventLog.h typed append-only
//                          event-log surface under fixy::sess::eventlog::*
//                          (8 strong IDs + SessionOp + 3 classifier enums
//                          + 3 op helpers + CipherEventPayload + SessionEvent
//                          + StepIdKeyFn/StepIdLess + 3 hash-helper templates
//                          + SessionEventLog) — used by Cipher.h HEAD/log
//                          roll-forward + cold-tier SessionEvent drain
//                          (FIXY-U-052d)
//   - fixy/Struct.h      — structural (non-Graded) safety wrappers
//                          (Pinned / NonMovable / NotInherited /
//                          FinalBy / Checked.h primitives /
//                          ConstantTime / Simd facade / OwnedRegion /
//                          Workload parallel_for_views family)
//   - fixy/Substr.h      — per-substrate session mints
//                          (SPSC / SWMR / ChaseLev / MetaLog / ChainEdge /
//                          MPMC / CalendarGrid / ShardedCalendarGrid /
//                          ShardedGrid / Snapshot)
//   - fixy/Wrap.h        — value-level safety wrappers (Refined /
//                          SealedRefined / Tagged / Monotonic /
//                          AppendOnly / Stale / TimeOrdered /
//                          WriteOnce / WriteOnceNonNull /
//                          BoundedMonotonic / OrderedAppendOnly /
//                          AtomicMonotonic — plus Linear / Secret /
//                          SharedPermission re-exports for one-stop
//                          `fixy::wrap::` value-wrapping access)
//
// ── Phase D (foundation universe re-exports, shipped) ─────────────
//   - fixy/Algebra.h     — algebra/Graded.h substrate + 30 lattices
//                          + GradedWrapper concept + Modality enum +
//                          law-verifier helpers
//   - fixy/Diag.h        — safety/Diagnostic.h Category + Catalog +
//                          tag_of_t / category_of_v bijection + 28
//                          tag classes + stable_name / type_id /
//                          function_id + canonicalize_pack +
//                          insight_provider + row_hash_contribution
//   - fixy/Eff.h         — effects/* Met(X) surface: Row<>,
//                          Computation<>, Subrow, row_union_t /
//                          difference_t / intersection_t, F*
//                          aliases (Pure/Tot/Ghost/Div/ST/All),
//                          Capability<>, ExecCtx + 5 canonical ctxs,
//                          21+ Resource budget tags, ConcurrentRow,
//                          OsUniverse, EffectMask
//   - fixy/Insights.h    — per-tag `insight_provider` specializations
//                          for the 20 FixyNotEngaged_<Axis> tags +
//                          §30.14 Theory.h corpus entries (Hunt-Sands
//                          axis-mismatch / Volpano-Smith-Irvine /
//                          SS09 unsoundness witnesses) — populates
//                          Why / Symptom / Correct / Violating prose
//                          for the deep-diagnostic builder
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
#include <crucible/fixy/Profile.h>
#include <crucible/fixy/Rules.h>

// ── Phase B — Fn aggregator ───────────────────────────────────────
#include <crucible/fixy/Fn.h>

// ── Phase C — substrate alias re-exports ──────────────────────────
#include <crucible/fixy/Bridge.h>
#include <crucible/fixy/Cap.h>
#include <crucible/fixy/Contract.h>
#include <crucible/fixy/Decide.h>
#include <crucible/fixy/Is.h>
#include <crucible/fixy/Mach.h>
#include <crucible/fixy/Perm.h>
#include <crucible/fixy/Handle.h>
#include <crucible/fixy/Pipe.h>
#include <crucible/fixy/Safety.h>
#include <crucible/fixy/Sess.h>
#include <crucible/fixy/Mpst.h>
#include <crucible/fixy/SessDecl.h>
#include <crucible/fixy/SessCT.h>
#include <crucible/fixy/SessContentAddr.h>
#include <crucible/fixy/SessEventLog.h>
#include <crucible/fixy/SessSubtype.h>
#include <crucible/fixy/SessQueue.h>
#include <crucible/fixy/Struct.h>
#include <crucible/fixy/Substr.h>
#include <crucible/fixy/Wrap.h>

// ── Phase D — foundation universe re-exports ─────────────────────
#include <crucible/fixy/Algebra.h>
#include <crucible/fixy/Diag.h>
#include <crucible/fixy/Eff.h>
#include <crucible/fixy/Insights.h>
#include <crucible/fixy/Modality.h>
#include <crucible/fixy/Source.h>
