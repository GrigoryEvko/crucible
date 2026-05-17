#pragma once

// ── crucible::fixy::is — Concept-gate aliases under fixy:: ─────────
//
// Phase C re-export.  Surfaces every safety/Is*.h concept gate under
// `fixy::is::IsX` so callers who include only the fixy umbrella never
// have to descend into the safety/ tree to constrain a template
// parameter.  Companion to:
//
//   - fixy/Safety.h — Linear / Secret / ScopedView token mints
//   - fixy/Mach.h   — Machine token mint
//   - fixy/Perm.h   — Permission / SharedPermission token mints
//   - fixy/Wrap.h   — Graded-backed value wrappers
//   - fixy/Struct.h — Non-Graded structural wrappers (Pinned, FinalBy,
//                     Checked, ConstantTime, Simd, OwnedRegion,
//                     Workload)
//
// Per CLAUDE.md L0 §Safety, each Is*.h ships a `concept IsX` plus a
// matching `is_x_v<T>` trait, BOTH living in
// `crucible::safety::extract`.  Concepts cannot be re-exported by a
// plain `using` declaration (a concept name is not a typedef), so we
// re-define each one as a template alias that delegates to the
// substrate concept.  The trait variable IS re-exportable via `using`.
//
// ── Scope ──────────────────────────────────────────────────────────
//
// Owns: safety/Is*.h (the 32 wrapper-recognizer + handle-shape
// concepts, all in `safety::extract`), safety/witness/IsWitness.h
// (the witness-tier concept in `safety::witness`).
//
// Does NOT own: effects/Capability.h (IsCapability), effects/ExecCtx.h
// (IsExecCtx + family), concurrent/Substrate.h (IsSubstrate +
// family), concurrent/SubstrateSessionBridge.h (IsBridgeable*),
// effects/FxAliases.h (IsPure / IsTot / IsGhost / IsDiv / IsST /
// IsAll), effects/Computation.h (IsComputation), effects/Concurrent.h
// (IsConcurrentRow).  Those live with the algebra+effects+diag fixy
// surface owned by the sibling Algebra/Eff fixy agent.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   TypeSafe — every alias preserves the substrate concept's
//              specialization-based recognizer logic (e.g.
//              `IsLinear<T>` is satisfied iff T is `safety::Linear<U>`
//              for some U).
//   DetSafe  — bit-identical concept evaluation across the alias path.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Concept aliases evaluate to the substrate concept at compile
// time; the alias is a name-lookup directive, not a runtime cost.

#include <crucible/safety/IsAllocClass.h>
#include <crucible/safety/IsBits.h>
#include <crucible/safety/IsBorrowed.h>
#include <crucible/safety/IsBorrowedRef.h>
#include <crucible/safety/IsBudgeted.h>
#include <crucible/safety/IsCipherTier.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsConsumerHandle.h>
#include <crucible/safety/IsCrash.h>
#include <crucible/safety/IsDetSafe.h>
#include <crucible/safety/IsEpochVersioned.h>
#include <crucible/safety/IsHotPath.h>
#include <crucible/safety/IsLinear.h>
#include <crucible/safety/IsMemOrder.h>
#include <crucible/safety/IsNumaPlacement.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsPermission.h>
#include <crucible/safety/IsProducerHandle.h>
#include <crucible/safety/IsProgress.h>
#include <crucible/safety/IsRecipeSpec.h>
#include <crucible/safety/IsReduceInto.h>
#include <crucible/safety/IsRefined.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsSecret.h>
#include <crucible/safety/IsSessionHandle.h>
#include <crucible/safety/IsStale.h>
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/safety/IsTagged.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/safety/IsWait.h>
#include <crucible/safety/witness/IsWitness.h>

namespace crucible::fixy::is {

// ─── Graded-backed value-wrapper recognizers ──────────────────────
//
// All concepts live in `crucible::safety::extract`; re-define each as
// a template alias delegating to the substrate concept.

template <typename T> concept IsAllocClass     = ::crucible::safety::extract::IsAllocClass<T>;
template <typename T> concept IsBits           = ::crucible::safety::extract::IsBits<T>;
template <typename T> concept IsBorrowed       = ::crucible::safety::extract::IsBorrowed<T>;
template <typename T> concept IsBorrowedRef    = ::crucible::safety::extract::IsBorrowedRef<T>;
template <typename T> concept IsBudgeted       = ::crucible::safety::extract::IsBudgeted<T>;
template <typename T> concept IsCipherTier     = ::crucible::safety::extract::IsCipherTier<T>;
template <typename T> concept IsConsistency    = ::crucible::safety::extract::IsConsistency<T>;
template <typename T> concept IsCrash          = ::crucible::safety::extract::IsCrash<T>;
template <typename T> concept IsDetSafe        = ::crucible::safety::extract::IsDetSafe<T>;
template <typename T> concept IsEpochVersioned = ::crucible::safety::extract::IsEpochVersioned<T>;
template <typename T> concept IsHotPath        = ::crucible::safety::extract::IsHotPath<T>;
template <typename T> concept IsLinear         = ::crucible::safety::extract::IsLinear<T>;
template <typename T> concept IsMemOrder       = ::crucible::safety::extract::IsMemOrder<T>;
template <typename T> concept IsNumaPlacement  = ::crucible::safety::extract::IsNumaPlacement<T>;
template <typename T> concept IsNumericalTier  = ::crucible::safety::extract::IsNumericalTier<T>;
template <typename T> concept IsOpaqueLifetime = ::crucible::safety::extract::IsOpaqueLifetime<T>;
template <typename T> concept IsProgress       = ::crucible::safety::extract::IsProgress<T>;
template <typename T> concept IsRecipeSpec     = ::crucible::safety::extract::IsRecipeSpec<T>;
template <typename T> concept IsReduceInto     = ::crucible::safety::extract::IsReduceInto<T>;
template <typename T> concept IsRefined        = ::crucible::safety::extract::IsRefined<T>;
template <typename T> concept IsResidencyHeat  = ::crucible::safety::extract::IsResidencyHeat<T>;
template <typename T> concept IsSecret         = ::crucible::safety::extract::IsSecret<T>;
template <typename T> concept IsStale          = ::crucible::safety::extract::IsStale<T>;
template <typename T> concept IsTagged         = ::crucible::safety::extract::IsTagged<T>;
template <typename T> concept IsVendor         = ::crucible::safety::extract::IsVendor<T>;
template <typename T> concept IsWait           = ::crucible::safety::extract::IsWait<T>;

// ─── Structural-wrapper recognizers ───────────────────────────────

template <typename T> concept IsOwnedRegion    = ::crucible::safety::extract::IsOwnedRegion<T>;

// ─── Permission tokens (linear + fractional) ──────────────────────

template <typename T> concept IsPermission       = ::crucible::safety::extract::IsPermission<T>;
template <typename T> concept IsSharedPermission = ::crucible::safety::extract::IsSharedPermission<T>;

template <typename T, typename Tag>
concept IsPermissionFor =
    ::crucible::safety::extract::IsPermissionFor<T, Tag>;

template <typename T, typename Tag>
concept IsSharedPermissionFor =
    ::crucible::safety::extract::IsSharedPermissionFor<T, Tag>;

// ─── Session handles (protocol typestate) ─────────────────────────

template <typename T> concept IsSessionHandle = ::crucible::safety::extract::IsSessionHandle<T>;

// ─── SPSC / SWMR handle shapes (substrate concepts) ───────────────

template <typename T> concept IsConsumerHandle = ::crucible::safety::extract::IsConsumerHandle<T>;
template <typename T> concept IsProducerHandle = ::crucible::safety::extract::IsProducerHandle<T>;
template <typename T> concept IsSwmrReader     = ::crucible::safety::extract::IsSwmrReader<T>;
template <typename T> concept IsSwmrWriter     = ::crucible::safety::extract::IsSwmrWriter<T>;

// ─── Witness-tier recognizer (proof-relevance lattice) ────────────

template <typename W>
concept IsWitness = ::crucible::safety::witness::IsWitness<W>;

template <typename W, typename Min>
concept WitnessAtLeast = ::crucible::safety::witness::WitnessAtLeast<W, Min>;

// ─── Trait re-exports (variable templates are using-decl-eligible) ─
//
// Concepts are not using-decl-eligible by name, but the matching
// `is_x_v<T>` traits in `crucible::safety::extract` are.  Re-export so
// callers using SFINAE or `if constexpr (fixy::is::is_linear_v<T>)`
// paths have a clean surface too.

using ::crucible::safety::extract::is_alloc_class_v;
using ::crucible::safety::extract::is_bits_v;
using ::crucible::safety::extract::is_borrowed_v;
using ::crucible::safety::extract::is_borrowed_ref_v;
using ::crucible::safety::extract::is_budgeted_v;
using ::crucible::safety::extract::is_cipher_tier_v;
using ::crucible::safety::extract::is_consistency_v;
using ::crucible::safety::extract::is_consumer_handle_v;
using ::crucible::safety::extract::is_crash_v;
using ::crucible::safety::extract::is_det_safe_v;
using ::crucible::safety::extract::is_epoch_versioned_v;
using ::crucible::safety::extract::is_hot_path_v;
using ::crucible::safety::extract::is_linear_v;
using ::crucible::safety::extract::is_mem_order_v;
using ::crucible::safety::extract::is_numa_placement_v;
using ::crucible::safety::extract::is_numerical_tier_v;
using ::crucible::safety::extract::is_opaque_lifetime_v;
using ::crucible::safety::extract::is_owned_region_v;
using ::crucible::safety::extract::is_permission_v;
using ::crucible::safety::extract::is_producer_handle_v;
using ::crucible::safety::extract::is_progress_v;
using ::crucible::safety::extract::is_recipe_spec_v;
using ::crucible::safety::extract::is_reduce_into_v;
using ::crucible::safety::extract::is_refined_v;
using ::crucible::safety::extract::is_residency_heat_v;
using ::crucible::safety::extract::is_secret_v;
using ::crucible::safety::extract::is_session_handle_v;
using ::crucible::safety::extract::is_shared_permission_v;
using ::crucible::safety::extract::is_stale_v;
using ::crucible::safety::extract::is_swmr_reader_v;
using ::crucible::safety::extract::is_swmr_writer_v;
using ::crucible::safety::extract::is_tagged_v;
using ::crucible::safety::extract::is_vendor_v;
using ::crucible::safety::extract::is_wait_v;

}  // namespace crucible::fixy::is
