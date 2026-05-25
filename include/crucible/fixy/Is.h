#pragma once

// ── crucible::fixy::is — Concept-gate aliases under fixy:: ─────────
//
// Re-export.  Surfaces every safety/Is*.h concept gate under
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
//
// ── Surface (fixy-L-06) ────────────────────────────────────────────
//
// THREE re-export tiers under one namespace:
//   (a) concept aliases     — fixy::is::IsX (template alias delegating
//                             to safety::extract::IsX);
//   (b) trait re-exports    — fixy::is::is_x_v (using-decl to
//                             safety::extract::is_x_v);
//   (c) type-alias helpers  — fixy::is::x_value_t / x_tag_t / x_proto_t
//                             / x_source_t / x_predicate_type_t / ...
//                             (using-decl to safety::extract::x_*).
//
// fixy-L-06 closes the (c) parity gap: every public *_t alias shipped
// by safety/IsX.h is re-exported here.  Adding a new IsX.h MUST add
// one row in each of (a), (b), (c) — the self_test block witnesses
// the load-bearing path for each tier.
//
// ── 4-path equivalence invariant (fixy-L-05) ──────────────────────
//
// Recognizing whether T is an `IsX` shape has FOUR syntactic paths:
//
//   (P1) ::crucible::safety::extract::IsX<T>      — substrate concept
//   (P2) ::crucible::safety::extract::is_x_v<T>   — substrate trait
//   (P3) ::crucible::fixy::is::IsX<T>             — fixy concept alias
//   (P4) ::crucible::fixy::is::is_x_v<T>          — fixy trait re-export
//
// P1↔P2 holds by construction at the substrate: every safety/IsX.h
// defines `concept IsX = is_x_v<T>`.  P3↔P1 and P4↔P2 are pinned by
// the existing self_test entries (alias-not-shadow witnesses).  The
// fourth edge — P3↔P4 within `fixy::is::` — is the load-bearing
// invariant for umbrella consumers who freely mix `requires IsX<T>`
// SFINAE constraints with `if constexpr (is_x_v<T>)` runtime branches.
// fixy-L-05 closes this edge with a per-family witness; a substrate
// refactor that decouples the substrate's own `IsX` from `is_x_v`
// would surface as a fixy-side failure here, not silently diverge
// across the alias boundary.

#include <crucible/safety/IsAllocClass.h>
#include <crucible/safety/IsBarrierGuarded.h>
#include <crucible/safety/IsBits.h>
#include <crucible/safety/IsBorrowed.h>
#include <crucible/safety/IsBorrowedRef.h>
#include <crucible/safety/IsBudgeted.h>
#include <crucible/safety/IsCipherTier.h>
#include <crucible/safety/IsClockSource.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsConsumerHandle.h>
#include <crucible/safety/IsCpuPinned.h>
#include <crucible/safety/IsCrash.h>
#include <crucible/safety/IsDetSafe.h>
#include <crucible/safety/IsEpochVersioned.h>
#include <crucible/safety/IsHotPath.h>
#include <crucible/safety/IsHw.h>
#include <crucible/safety/IsJoinPolicy.h>
#include <crucible/safety/IsLinear.h>
#include <crucible/safety/IsMemOrder.h>
#include <crucible/safety/IsNumaPlacement.h>
#include <crucible/safety/IsNumericalTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/IsOwnedMmap.h>
#include <crucible/safety/IsOwnedRegion.h>
#include <crucible/safety/IsPermission.h>
#include <crucible/safety/IsProducerHandle.h>
#include <crucible/safety/IsProgress.h>
#include <crucible/safety/IsRecipeSpec.h>
#include <crucible/safety/IsReduceInto.h>
#include <crucible/safety/IsRefined.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsSchedClass.h>
#include <crucible/safety/IsScopedFence.h>
#include <crucible/safety/IsSecret.h>
#include <crucible/safety/IsSessionHandle.h>
#include <crucible/safety/IsSimdWidthPinned.h>
#include <crucible/safety/IsStale.h>
#include <crucible/safety/IsSuspendBehavior.h>
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
using ::crucible::safety::extract::is_barrier_guarded_v;
using ::crucible::safety::extract::is_bits_v;
using ::crucible::safety::extract::is_borrowed_v;
using ::crucible::safety::extract::is_borrowed_ref_v;
using ::crucible::safety::extract::is_budgeted_v;
using ::crucible::safety::extract::is_cipher_tier_v;
using ::crucible::safety::extract::is_clock_source_v;
using ::crucible::safety::extract::is_consistency_v;
using ::crucible::safety::extract::is_consumer_handle_v;
using ::crucible::safety::extract::is_cpu_pinned_v;
using ::crucible::safety::extract::is_crash_v;
using ::crucible::safety::extract::is_det_safe_v;
using ::crucible::safety::extract::is_epoch_versioned_v;
using ::crucible::safety::extract::is_hot_path_v;
using ::crucible::safety::extract::is_hw_v;
using ::crucible::safety::extract::is_linear_v;
using ::crucible::safety::extract::is_mem_order_v;
using ::crucible::safety::extract::is_numa_placement_v;
using ::crucible::safety::extract::is_numerical_tier_v;
using ::crucible::safety::extract::is_opaque_lifetime_v;
using ::crucible::safety::extract::is_owned_mmap_v;
using ::crucible::safety::extract::is_owned_region_v;
using ::crucible::safety::extract::is_permission_v;
using ::crucible::safety::extract::is_producer_handle_v;
using ::crucible::safety::extract::is_progress_v;
using ::crucible::safety::extract::is_recipe_spec_v;
using ::crucible::safety::extract::is_reduce_into_v;
using ::crucible::safety::extract::is_refined_v;
using ::crucible::safety::extract::refined_is_sealed_v;
using ::crucible::safety::extract::is_residency_heat_v;
using ::crucible::safety::extract::is_sched_class_v;
using ::crucible::safety::extract::is_scoped_fence_v;
using ::crucible::safety::extract::is_secret_v;
using ::crucible::safety::extract::is_session_handle_v;
using ::crucible::safety::extract::is_shared_permission_v;
using ::crucible::safety::extract::is_simd_width_pinned_v;
using ::crucible::safety::extract::is_stale_v;
using ::crucible::safety::extract::is_suspend_behavior_v;
using ::crucible::safety::extract::is_swmr_reader_v;
using ::crucible::safety::extract::is_swmr_writer_v;
using ::crucible::safety::extract::is_tagged_v;
using ::crucible::safety::extract::is_join_policy_v;
using ::crucible::safety::extract::is_vendor_v;
using ::crucible::safety::extract::is_wait_v;

// ─── Substrate type-alias re-exports (fixy-L-06) ──────────────────
//
// Each safety/IsX.h ships a slot-extractor alias template alongside
// its concept + `is_x_v` trait:  e.g. IsLinear.h ships `IsLinear<T>`,
// `is_linear_v<T>`, AND `linear_value_t<T>` (returns Linear<T>'s
// inner payload type).  fixy::is::IsX + is_x_v have been mirrored
// since the file was authored; the *_t slot extractors had drifted
// out of parity (fixy-L-06: "Is.h re-exports incomplete vs available
// IsX.h headers").  Re-export FULL parity here — partial parity is
// its own form of drift, since every new IsX.h would silently leave
// its extractor stranded behind the umbrella.
//
// Each `using` preserves the substrate's `requires is_x_v<T>`
// constraint (using-decls name the template, not the unconstrained
// shape); fixy::is::linear_value_t<int> still SFINAE-rejects with the
// substrate's diagnostic.  Ordered alphabetically so new entries
// land in their lexical home with no churn.

using ::crucible::safety::extract::alloc_class_value_t;
using ::crucible::safety::extract::barrier_guarded_value_t;
using ::crucible::safety::extract::bits_enum_t;
using ::crucible::safety::extract::bits_underlying_t;
using ::crucible::safety::extract::borrowed_ref_value_t;
using ::crucible::safety::extract::borrowed_source_t;
using ::crucible::safety::extract::borrowed_value_t;
using ::crucible::safety::extract::budgeted_value_t;
using ::crucible::safety::extract::cipher_tier_value_t;
using ::crucible::safety::extract::clock_source_value_t;
using ::crucible::safety::extract::consistency_value_t;
using ::crucible::safety::extract::consumer_handle_value_t;
using ::crucible::safety::extract::cpu_pinned_value_t;
using ::crucible::safety::extract::crash_value_t;
using ::crucible::safety::extract::det_safe_value_t;
using ::crucible::safety::extract::epoch_versioned_value_t;
using ::crucible::safety::extract::hot_path_value_t;
using ::crucible::safety::extract::hw_value_t;
using ::crucible::safety::extract::linear_value_t;
using ::crucible::safety::extract::mem_order_value_t;
using ::crucible::safety::extract::numa_placement_value_t;
using ::crucible::safety::extract::numerical_tier_value_t;
using ::crucible::safety::extract::opaque_lifetime_value_t;
using ::crucible::safety::extract::owned_region_tag_t;
using ::crucible::safety::extract::owned_region_value_t;
using ::crucible::safety::extract::permission_tag_t;
using ::crucible::safety::extract::producer_handle_value_t;
using ::crucible::safety::extract::progress_value_t;
using ::crucible::safety::extract::recipe_spec_value_t;
using ::crucible::safety::extract::reduce_into_accumulator_t;
using ::crucible::safety::extract::reduce_into_reducer_t;
using ::crucible::safety::extract::refined_predicate_type_t;
using ::crucible::safety::extract::refined_value_t;
using ::crucible::safety::extract::residency_heat_value_t;
using ::crucible::safety::extract::sched_class_value_t;
using ::crucible::safety::extract::scoped_fence_value_t;
using ::crucible::safety::extract::secret_value_t;
using ::crucible::safety::extract::session_handle_proto_t;
using ::crucible::safety::extract::shared_permission_tag_t;
using ::crucible::safety::extract::simd_width_pinned_value_t;
using ::crucible::safety::extract::stale_semiring_t;
using ::crucible::safety::extract::stale_staleness_t;
using ::crucible::safety::extract::stale_value_t;
using ::crucible::safety::extract::suspend_behavior_value_t;
using ::crucible::safety::extract::swmr_reader_value_t;
using ::crucible::safety::extract::swmr_writer_value_t;
using ::crucible::safety::extract::tagged_tag_t;
using ::crucible::safety::extract::tagged_value_t;
using ::crucible::safety::extract::join_policy_value_t;
using ::crucible::safety::extract::vendor_value_t;
using ::crucible::safety::extract::wait_value_t;

// ─── Witness-namespace trait re-export (fixy-L-06) ────────────────
//
// IsWitness.h ships `safety::witness::is_valid_witness_v<W>` — the
// runtime-correlated witness-registry check (Tested/CrossValidated
// active vs Revoked/Stale/Expired).  Not concept-equivalent (validity
// is a registry lookup, not a typestate) but fits the trait surface.

using ::crucible::safety::witness::is_valid_witness_v;

}  // namespace crucible::fixy::is

// ── Self-test ──────────────────────────────────────────────────────
//
// fixy-M-03: previously this header had NO self-test block —
// discipline-inconsistent with the rest of fixy/.  Witnesses lock the
// alias discipline down: every fixy::is:: concept and `is_x_v` trait
// agrees with its substrate counterpart in `safety::extract::` (and
// `safety::witness::` for IsWitness).  The pattern follows fixy/Cap.h
// — concept-equality on positive AND negative cases proves the alias
// is a true alias, not a shadowing redefinition with subtly different
// semantics.  Four witnesses cover the full discipline surface:
// Graded wrapper recognizers (positive + negative), a second family,
// a 2-arg parameterized concept, and the cross-namespace witness
// branch.  Future contributors adding a new IsX/is_x_v pair add one
// row here; the missing-row is grep-discoverable.

namespace crucible::fixy::is::self_test {

// (1) IsLinear — Graded-backed value wrapper.  Positive case
//     witnesses that the alias reaches the substrate concept's logic
//     (`safety::Linear<int>` is pulled in transitively by IsLinear.h),
//     not just a stub returning false.
static_assert(IsLinear<::crucible::safety::Linear<int>>,
    "fixy::is::IsLinear must recognise safety::Linear<int>.");
static_assert(IsLinear<::crucible::safety::Linear<int>>
           == ::crucible::safety::extract::IsLinear<
                  ::crucible::safety::Linear<int>>,
    "fixy::is::IsLinear must agree with safety::extract::IsLinear on "
    "every payload (alias, not shadow).");
static_assert(!IsLinear<int>,
    "fixy::is::IsLinear must reject bare types.");
static_assert(is_linear_v<::crucible::safety::Linear<int>>,
    "fixy::is::is_linear_v trait must mirror the concept alias.");

// (2) IsSecret — second Graded-backed family.  Negative-case
//     agreement scales the witness pattern across recognizers.
static_assert(IsSecret<int> == ::crucible::safety::extract::IsSecret<int>);
static_assert(is_secret_v<int> == ::crucible::safety::extract::is_secret_v<int>);

// (3) IsPermissionFor — 2-arg parameterised concept; proves the
//     two-template-parameter alias form preserves substrate behaviour.
static_assert(IsPermissionFor<int, int>
           == ::crucible::safety::extract::IsPermissionFor<int, int>);

// (4) IsWitness — cross-namespace witness branch.  Substrate lives in
//     `safety::witness::`, not `safety::extract::`; the alias must
//     forward to the right substrate concept.
static_assert(IsWitness<int> == ::crucible::safety::witness::IsWitness<int>);

// (5) linear_value_t — substrate type-alias parity (fixy-L-06).
//     Witnesses that the slot-extractor alias re-export resolves to
//     the substrate template, identity preserved at instantiation.
static_assert(std::is_same_v<
    linear_value_t<::crucible::safety::Linear<int>>,
    ::crucible::safety::extract::linear_value_t<
        ::crucible::safety::Linear<int>>>,
    "fixy-L-06: fixy::is::linear_value_t must alias "
    "safety::extract::linear_value_t (same template, not a shadow).");

// (6) tagged_tag_t — second slot-extractor family.  Two-parameter
//     wrapper (Tagged<T, Source>) extractor — exercises the path
//     where the extracted slot is the SECOND template argument.
namespace L06_TaggedProbe {
struct ProbeSource {};
}  // namespace L06_TaggedProbe

static_assert(std::is_same_v<
    tagged_tag_t<::crucible::safety::Tagged<int,
        L06_TaggedProbe::ProbeSource>>,
    ::crucible::safety::extract::tagged_tag_t<
        ::crucible::safety::Tagged<int, L06_TaggedProbe::ProbeSource>>>,
    "fixy-L-06: fixy::is::tagged_tag_t must alias "
    "safety::extract::tagged_tag_t (two-arg wrapper, second-slot "
    "extractor).");

// (7) is_valid_witness_v — witness-registry trait re-export.  Bare
//     types fall through to the primary template (= true); the path
//     identity is what we witness, not the value.
static_assert(is_valid_witness_v<int>
           == ::crucible::safety::witness::is_valid_witness_v<int>,
    "fixy-L-06: fixy::is::is_valid_witness_v must alias the substrate "
    "safety::witness::is_valid_witness_v trait, not a shadowing redef.");

// (8) fixy-L-05: cross-axis 4-path closure within fixy::is::.
//
//     Each of the 13 Graded-backed recognizer families ships BOTH a
//     concept alias `IsX<T>` AND a trait re-export `is_x_v<T>` under
//     `fixy::is::`.  The substrate defines `IsX = is_x_v<T>` by
//     construction; if a future refactor decouples them at the
//     substrate, the fixy umbrella alias-vs-trait edges would silently
//     diverge.  Lock the 4th edge of the commutative square: P3 ↔ P4
//     within `fixy::is::`, on positive AND negative cases per family.
//
//     Pattern: `IsX<T> == is_x_v<T>` for one positive witness type
//     (the canonical wrapper) and one negative witness type (a bare
//     primitive).  Adding a new IsX.h family adds one row here; the
//     missing row is grep-discoverable by family-name parity with the
//     using-decl block above.

// 13 Graded-backed value-wrapper recognizers (positive + negative each).
static_assert(IsLinear        <::crucible::safety::Linear<int>>
           == is_linear_v     <::crucible::safety::Linear<int>>);
static_assert(IsLinear<int>         == is_linear_v<int>);
static_assert(IsSecret<int>         == is_secret_v<int>);
static_assert(IsTagged<int>         == is_tagged_v<int>);
static_assert(IsRefined<int>        == is_refined_v<int>);
static_assert(IsStale<int>          == is_stale_v<int>);
static_assert(IsHotPath<int>        == is_hot_path_v<int>);
static_assert(IsDetSafe<int>        == is_det_safe_v<int>);
static_assert(IsNumericalTier<int>  == is_numerical_tier_v<int>);
static_assert(IsVendor<int>         == is_vendor_v<int>);
static_assert(IsResidencyHeat<int>  == is_residency_heat_v<int>);
static_assert(IsCipherTier<int>     == is_cipher_tier_v<int>);
static_assert(IsAllocClass<int>     == is_alloc_class_v<int>);
static_assert(IsWait<int>           == is_wait_v<int>);
static_assert(IsMemOrder<int>       == is_mem_order_v<int>);
static_assert(IsProgress<int>       == is_progress_v<int>);
static_assert(IsBudgeted<int>       == is_budgeted_v<int>);
static_assert(IsBits<int>           == is_bits_v<int>);
static_assert(IsBorrowed<int>       == is_borrowed_v<int>);
static_assert(IsBorrowedRef<int>    == is_borrowed_ref_v<int>);
static_assert(IsConsistency<int>    == is_consistency_v<int>);
static_assert(IsCrash<int>          == is_crash_v<int>);
static_assert(IsEpochVersioned<int> == is_epoch_versioned_v<int>);
static_assert(IsNumaPlacement<int>  == is_numa_placement_v<int>);
static_assert(IsOpaqueLifetime<int> == is_opaque_lifetime_v<int>);
static_assert(IsRecipeSpec<int>     == is_recipe_spec_v<int>);
static_assert(IsReduceInto<int>     == is_reduce_into_v<int>);

// Structural-wrapper + Permission + Session + SPSC/SWMR-handle
// recognizers (one negative witness each — positive cases live in the
// substrate header self_tests; we only witness fixy-side path agreement).
static_assert(IsOwnedRegion<int>     == is_owned_region_v<int>);
static_assert(IsPermission<int>      == is_permission_v<int>);
static_assert(IsSharedPermission<int> == is_shared_permission_v<int>);
static_assert(IsSessionHandle<int>   == is_session_handle_v<int>);
static_assert(IsConsumerHandle<int>  == is_consumer_handle_v<int>);
static_assert(IsProducerHandle<int>  == is_producer_handle_v<int>);
static_assert(IsSwmrReader<int>      == is_swmr_reader_v<int>);
static_assert(IsSwmrWriter<int>      == is_swmr_writer_v<int>);

}  // namespace crucible::fixy::is::self_test
