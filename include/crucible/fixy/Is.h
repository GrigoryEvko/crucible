#pragma once

// The recognizer concepts and traits below live in crucible::safety::extract,
// and one of them in crucible::safety::witness.  The re-export gives a caller
// that pulls in only the fixy surface an entry point that does not name those
// namespaces.
//
// A concept name is not a typedef, so a using-declaration cannot carry one
// across.  Each concept is therefore re-declared here as an alias that
// delegates to the substrate concept, while the matching traits and slot
// extractors come across as plain using-declarations.

#include <crucible/safety/_IsAllocClass.h>
#include <crucible/safety/IsBarrierGuarded.h>
#include <crucible/safety/IsBits.h>
#include <crucible/safety/_IsBorrowed.h>
#include <crucible/safety/_IsBorrowedRef.h>
#include <crucible/safety/IsBudgeted.h>
#include <crucible/safety/_IsCipherTier.h>
#include <crucible/safety/IsClockSource.h>
#include <crucible/safety/IsConsistency.h>
#include <crucible/safety/IsConsumerHandle.h>
#include <crucible/safety/IsCpuPinned.h>
#include <crucible/safety/IsCrash.h>
#include <crucible/safety/_IsDetSafe.h>
#include <crucible/safety/IsEpochVersioned.h>
#include <crucible/safety/_IsHotPath.h>
#include <crucible/safety/IsHw.h>
#include <crucible/safety/IsJoinPolicy.h>
#include <crucible/safety/IsLinear.h>
#include <crucible/safety/IsMemOrder.h>
#include <crucible/safety/IsNumaPlacement.h>
#include <crucible/safety/_IsNumericalTier.h>
#include <crucible/safety/_IsOpaqueLifetime.h>
#include <crucible/safety/IsOwnedMmap.h>
#include <crucible/safety/_IsOwnedRegion.h>
#include <crucible/safety/IsPermission.h>
#include <crucible/safety/IsProducerHandle.h>
#include <crucible/safety/IsProgress.h>
#include <crucible/safety/_IsRecipeSpec.h>
#include <crucible/safety/IsReduceInto.h>
#include <crucible/safety/_IsRefined.h>
#include <crucible/safety/IsResidencyHeat.h>
#include <crucible/safety/IsSchedClass.h>
#include <crucible/safety/_IsScopedFence.h>
#include <crucible/safety/_IsSecret.h>
#include <crucible/safety/IsSessionHandle.h>
#include <crucible/safety/IsSimdWidthPinned.h>
#include <crucible/safety/_IsStale.h>
#include <crucible/safety/IsSuspendBehavior.h>
#include <crucible/safety/IsSwmrHandle.h>
#include <crucible/safety/_IsTagged.h>
#include <crucible/safety/IsVendor.h>
#include <crucible/safety/IsWait.h>
#include <crucible/safety/witness/IsWitness.h>

namespace crucible::fixy::is {

template <typename T>
concept IsAllocClass = ::crucible::safety::extract::IsAllocClass<T>;
template <typename T>
concept IsBits = ::crucible::safety::extract::IsBits<T>;
template <typename T>
concept IsBorrowed = ::crucible::safety::extract::IsBorrowed<T>;
template <typename T>
concept IsBorrowedRef = ::crucible::safety::extract::IsBorrowedRef<T>;
template <typename T>
concept IsBudgeted = ::crucible::safety::extract::IsBudgeted<T>;
template <typename T>
concept IsCipherTier = ::crucible::safety::extract::IsCipherTier<T>;
template <typename T>
concept IsConsistency = ::crucible::safety::extract::IsConsistency<T>;
template <typename T>
concept IsCrash = ::crucible::safety::extract::IsCrash<T>;
template <typename T>
concept IsDetSafe = ::crucible::safety::extract::IsDetSafe<T>;
template <typename T>
concept IsEpochVersioned = ::crucible::safety::extract::IsEpochVersioned<T>;
template <typename T>
concept IsHotPath = ::crucible::safety::extract::IsHotPath<T>;
template <typename T>
concept IsLinear = ::crucible::safety::extract::IsLinear<T>;
template <typename T>
concept IsMemOrder = ::crucible::safety::extract::IsMemOrder<T>;
template <typename T>
concept IsNumaPlacement = ::crucible::safety::extract::IsNumaPlacement<T>;
template <typename T>
concept IsNumericalTier = ::crucible::safety::extract::IsNumericalTier<T>;
template <typename T>
concept IsOpaqueLifetime = ::crucible::safety::extract::IsOpaqueLifetime<T>;
template <typename T>
concept IsProgress = ::crucible::safety::extract::IsProgress<T>;
template <typename T>
concept IsRecipeSpec = ::crucible::safety::extract::IsRecipeSpec<T>;
template <typename T>
concept IsReduceInto = ::crucible::safety::extract::IsReduceInto<T>;
template <typename T>
concept IsRefined = ::crucible::safety::extract::IsRefined<T>;
template <typename T>
concept IsResidencyHeat = ::crucible::safety::extract::IsResidencyHeat<T>;
template <typename T>
concept IsSecret = ::crucible::safety::extract::IsSecret<T>;
template <typename T>
concept IsStale = ::crucible::safety::extract::IsStale<T>;
template <typename T>
concept IsTagged = ::crucible::safety::extract::IsTagged<T>;
template <typename T>
concept IsVendor = ::crucible::safety::extract::IsVendor<T>;
template <typename T>
concept IsWait = ::crucible::safety::extract::IsWait<T>;

template <typename T>
concept IsOwnedRegion = ::crucible::safety::extract::IsOwnedRegion<T>;

template <typename T>
concept IsPermission = ::crucible::safety::extract::IsPermission<T>;
template <typename T>
concept IsSharedPermission = ::crucible::safety::extract::IsSharedPermission<T>;

template <typename T, typename Tag>
concept IsPermissionFor = ::crucible::safety::extract::IsPermissionFor<T, Tag>;

template <typename T, typename Tag>
concept IsSharedPermissionFor = ::crucible::safety::extract::IsSharedPermissionFor<T, Tag>;

template <typename T>
concept IsSessionHandle = ::crucible::safety::extract::IsSessionHandle<T>;

template <typename T>
concept IsConsumerHandle = ::crucible::safety::extract::IsConsumerHandle<T>;
template <typename T>
concept IsProducerHandle = ::crucible::safety::extract::IsProducerHandle<T>;
template <typename T>
concept IsSwmrReader = ::crucible::safety::extract::IsSwmrReader<T>;
template <typename T>
concept IsSwmrWriter = ::crucible::safety::extract::IsSwmrWriter<T>;

template <typename W>
concept IsWitness = ::crucible::safety::witness::IsWitness<W>;

template <typename W, typename Min>
concept WitnessAtLeast = ::crucible::safety::witness::WitnessAtLeast<W, Min>;

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

// A using-declaration names the constrained template, so each extractor
// keeps the substrate's requires-clause: naming one at a type that fails
// the matching trait still rejects with the substrate's own diagnostic.

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

// This trait has no matching concept above.  Witness validity is a registry
// lookup rather than a typestate, so it carries no recognizer form.

using ::crucible::safety::witness::is_valid_witness_v;

}  // namespace crucible::fixy::is

// Concept equality on a positive and a negative case proves each alias is a
// true alias rather than a shadowing redefinition that happens to agree on
// the case someone tried.

namespace crucible::fixy::is::self_test {

static_assert(IsLinear<::crucible::safety::Linear<int>>, "fixy::is::IsLinear must recognise safety::Linear<int>.");
static_assert(IsLinear<::crucible::safety::Linear<int>>
                  == ::crucible::safety::extract::IsLinear<::crucible::safety::Linear<int>>,
              "fixy::is::IsLinear must agree with safety::extract::IsLinear on "
              "every payload (alias, not shadow).");
static_assert(!IsLinear<int>, "fixy::is::IsLinear must reject bare types.");
static_assert(is_linear_v<::crucible::safety::Linear<int>>,
              "fixy::is::is_linear_v trait must mirror the concept alias.");

static_assert(IsSecret<int> == ::crucible::safety::extract::IsSecret<int>);
static_assert(is_secret_v<int> == ::crucible::safety::extract::is_secret_v<int>);

static_assert(IsPermissionFor<int, int> == ::crucible::safety::extract::IsPermissionFor<int, int>);

static_assert(IsWitness<int> == ::crucible::safety::witness::IsWitness<int>);

static_assert(std::is_same_v<linear_value_t<::crucible::safety::Linear<int>>,
                             ::crucible::safety::extract::linear_value_t<::crucible::safety::Linear<int>>>,
              "fixy::is::linear_value_t must alias safety::extract::linear_value_t "
              "(same template, not a shadow).");

namespace L06_TaggedProbe {
struct ProbeSource {};
}  // namespace L06_TaggedProbe

static_assert(
    std::is_same_v<
        tagged_tag_t<::crucible::safety::Tagged<int, L06_TaggedProbe::ProbeSource>>,
        ::crucible::safety::extract::tagged_tag_t<::crucible::safety::Tagged<int, L06_TaggedProbe::ProbeSource>>>,
    "fixy::is::tagged_tag_t must alias safety::extract::tagged_tag_t "
    "(two-arg wrapper, second-slot extractor).");

// A bare type falls through to the primary template.  The witness is path
// identity, not the resulting value.
static_assert(is_valid_witness_v<int> == ::crucible::safety::witness::is_valid_witness_v<int>,
              "fixy::is::is_valid_witness_v must alias the substrate "
              "safety::witness::is_valid_witness_v trait, not a shadowing redef.");

// The substrate defines each concept as its own trait, so the two halves of
// this surface agree today.  A substrate refactor that decoupled them would
// otherwise split a caller who mixes a requires-clause with an if-constexpr
// branch, and split it silently.  The rows below pin that agreement here.

static_assert(IsLinear<::crucible::safety::Linear<int>> == is_linear_v<::crucible::safety::Linear<int>>);
static_assert(IsLinear<int> == is_linear_v<int>);
static_assert(IsSecret<int> == is_secret_v<int>);
static_assert(IsTagged<int> == is_tagged_v<int>);
static_assert(IsRefined<int> == is_refined_v<int>);
static_assert(IsStale<int> == is_stale_v<int>);
static_assert(IsHotPath<int> == is_hot_path_v<int>);
static_assert(IsDetSafe<int> == is_det_safe_v<int>);
static_assert(IsNumericalTier<int> == is_numerical_tier_v<int>);
static_assert(IsVendor<int> == is_vendor_v<int>);
static_assert(IsResidencyHeat<int> == is_residency_heat_v<int>);
static_assert(IsCipherTier<int> == is_cipher_tier_v<int>);
static_assert(IsAllocClass<int> == is_alloc_class_v<int>);
static_assert(IsWait<int> == is_wait_v<int>);
static_assert(IsMemOrder<int> == is_mem_order_v<int>);
static_assert(IsProgress<int> == is_progress_v<int>);
static_assert(IsBudgeted<int> == is_budgeted_v<int>);
static_assert(IsBits<int> == is_bits_v<int>);
static_assert(IsBorrowed<int> == is_borrowed_v<int>);
static_assert(IsBorrowedRef<int> == is_borrowed_ref_v<int>);
static_assert(IsConsistency<int> == is_consistency_v<int>);
static_assert(IsCrash<int> == is_crash_v<int>);
static_assert(IsEpochVersioned<int> == is_epoch_versioned_v<int>);
static_assert(IsNumaPlacement<int> == is_numa_placement_v<int>);
static_assert(IsOpaqueLifetime<int> == is_opaque_lifetime_v<int>);
static_assert(IsRecipeSpec<int> == is_recipe_spec_v<int>);
static_assert(IsReduceInto<int> == is_reduce_into_v<int>);

// One negative witness each below.  The positive cases belong to the
// substrate headers; what matters here is that the two fixy paths agree.
static_assert(IsOwnedRegion<int> == is_owned_region_v<int>);
static_assert(IsPermission<int> == is_permission_v<int>);
static_assert(IsSharedPermission<int> == is_shared_permission_v<int>);
static_assert(IsSessionHandle<int> == is_session_handle_v<int>);
static_assert(IsConsumerHandle<int> == is_consumer_handle_v<int>);
static_assert(IsProducerHandle<int> == is_producer_handle_v<int>);
static_assert(IsSwmrReader<int> == is_swmr_reader_v<int>);
static_assert(IsSwmrWriter<int> == is_swmr_writer_v<int>);

}  // namespace crucible::fixy::is::self_test
