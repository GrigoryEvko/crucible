// Sentinel TU: compiles the alias header under the project warning flags so its
// concept aliases and trait re-exports are instantiated.

#include <crucible/fixy/Is.h>

#include <crucible/safety/_Linear.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/_Stale.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <type_traits>

namespace fis = ::crucible::fixy::is;
namespace saf = ::crucible::safety;
namespace sext = ::crucible::safety::extract;
namespace swit = ::crucible::safety::witness;

namespace {
struct PlainInt {
    int x = 0;
};

using SubLinear = saf::Linear<int>;
using SubSecret = saf::Secret<int>;
using SubRefined = saf::Refined<saf::positive, int>;

struct TagSource {};
using SubTagged = saf::Tagged<int, TagSource>;
}  // namespace

static_assert(fis::IsLinear<SubLinear>, "fixy::is::IsLinear accepts safety::Linear<T>.");
static_assert(!fis::IsLinear<PlainInt>, "fixy::is::IsLinear rejects unrelated types.");

static_assert(fis::IsSecret<SubSecret>, "fixy::is::IsSecret accepts safety::Secret<T>.");
static_assert(!fis::IsSecret<PlainInt>, "fixy::is::IsSecret rejects unrelated types.");

static_assert(fis::IsRefined<SubRefined>, "fixy::is::IsRefined accepts safety::Refined<Pred, T>.");
static_assert(!fis::IsRefined<PlainInt>, "fixy::is::IsRefined rejects unrelated types.");

static_assert(fis::IsTagged<SubTagged>, "fixy::is::IsTagged accepts safety::Tagged<T, S>.");
static_assert(!fis::IsTagged<PlainInt>, "fixy::is::IsTagged rejects unrelated types.");

static_assert(fis::is_linear_v<SubLinear> == sext::is_linear_v<SubLinear>,
              "fixy::is::is_linear_v must match the substrate trait.");
static_assert(fis::is_secret_v<SubSecret> == sext::is_secret_v<SubSecret>,
              "fixy::is::is_secret_v must match the substrate trait.");
static_assert(fis::is_refined_v<SubRefined> == sext::is_refined_v<SubRefined>,
              "fixy::is::is_refined_v must match the substrate trait.");
static_assert(fis::is_tagged_v<SubTagged> == sext::is_tagged_v<SubTagged>,
              "fixy::is::is_tagged_v must match the substrate trait.");
static_assert(fis::is_linear_v<PlainInt> == sext::is_linear_v<PlainInt>,
              "Negative trait result must match the substrate.");

namespace {
struct WitnessReason {};
using AssertedW = swit::Asserted<WitnessReason>;
}  // namespace

static_assert(fis::IsWitness<AssertedW>, "fixy::is::IsWitness must accept Asserted<R>.");
static_assert(!fis::IsWitness<PlainInt>, "fixy::is::IsWitness must reject non-witness types.");

static_assert(fis::WitnessAtLeast<AssertedW, AssertedW>,
              "fixy::is::WitnessAtLeast must be reflexive on the witness lattice.");

// ── crucible_fixy::is:: alias reach, one claim per symbol ──────────────────────
//
// fixy/Is.h re-exports the 96 public `_v` traits and `_t` slot
// extractors of crucible::safety::extract through using-declarations.
// Nothing here instantiates them: naming both spellings of one symbol
// in a single scope is legal only when the two name the SAME entity,
// so each pair below is a two-line proof that the re-export exists and
// resolves where it claims to.  The failure modes are distinct and both
// are compile errors:
//
//   * fixy/Is.h drops a row        -> "X has not been declared in
//                                      crucible::fixy::is"
//   * crucible_fixy::is::X names something
//     other than extract::X        -> "conflicts with a previous
//                                      declaration"
//
// The claims need no witness type, which is why every symbol can be
// covered rather than a sampled few — a constrained alias template such
// as `bits_enum_t` has no argument that is valid for every symbol, and
// a sampled gate is how a surface silently loses rows.
//
// Concepts are deliberately absent.  fixy/Is.h cannot carry a concept
// across by using-declaration, so it re-declares each one as a fresh
// concept that delegates to the substrate concept.  A fresh concept is
// by construction a different entity, so the pair claim does not apply;
// a missing concept surfaces as an ordinary compile error at the
// `crucible_fixy::is::IsX` use sites elsewhere in this TU and in production.
//
// This gate proves fidelity, not completeness: it says every symbol
// named here resolves correctly, not that the list covers the whole
// substrate surface.  Completeness is scripts/check-isx-parity.sh,
// which derives the demand set from the headers, so a newly added
// substrate alias is demanded without anyone editing this file.
namespace isx_alias_reach {
using ::crucible::fixy::is::alloc_class_value_t;
using ::crucible::safety::extract::alloc_class_value_t;
using ::crucible::fixy::is::barrier_guarded_value_t;
using ::crucible::safety::extract::barrier_guarded_value_t;
using ::crucible::fixy::is::bits_enum_t;
using ::crucible::safety::extract::bits_enum_t;
using ::crucible::fixy::is::bits_underlying_t;
using ::crucible::safety::extract::bits_underlying_t;
using ::crucible::fixy::is::borrowed_ref_value_t;
using ::crucible::safety::extract::borrowed_ref_value_t;
using ::crucible::fixy::is::borrowed_source_t;
using ::crucible::safety::extract::borrowed_source_t;
using ::crucible::fixy::is::borrowed_value_t;
using ::crucible::safety::extract::borrowed_value_t;
using ::crucible::fixy::is::budgeted_value_t;
using ::crucible::safety::extract::budgeted_value_t;
using ::crucible::fixy::is::cipher_tier_value_t;
using ::crucible::safety::extract::cipher_tier_value_t;
using ::crucible::fixy::is::clock_source_value_t;
using ::crucible::safety::extract::clock_source_value_t;
using ::crucible::fixy::is::consistency_value_t;
using ::crucible::safety::extract::consistency_value_t;
using ::crucible::fixy::is::consumer_handle_value_t;
using ::crucible::safety::extract::consumer_handle_value_t;
using ::crucible::fixy::is::cpu_pinned_value_t;
using ::crucible::safety::extract::cpu_pinned_value_t;
using ::crucible::fixy::is::crash_value_t;
using ::crucible::safety::extract::crash_value_t;
using ::crucible::fixy::is::det_safe_value_t;
using ::crucible::safety::extract::det_safe_value_t;
using ::crucible::fixy::is::epoch_versioned_value_t;
using ::crucible::safety::extract::epoch_versioned_value_t;
using ::crucible::fixy::is::hot_path_value_t;
using ::crucible::safety::extract::hot_path_value_t;
using ::crucible::fixy::is::hw_value_t;
using ::crucible::safety::extract::hw_value_t;
using ::crucible::fixy::is::is_alloc_class_v;
using ::crucible::safety::extract::is_alloc_class_v;
using ::crucible::fixy::is::is_barrier_guarded_v;
using ::crucible::safety::extract::is_barrier_guarded_v;
using ::crucible::fixy::is::is_bits_v;
using ::crucible::safety::extract::is_bits_v;
using ::crucible::fixy::is::is_borrowed_ref_v;
using ::crucible::safety::extract::is_borrowed_ref_v;
using ::crucible::fixy::is::is_borrowed_v;
using ::crucible::safety::extract::is_borrowed_v;
using ::crucible::fixy::is::is_budgeted_v;
using ::crucible::safety::extract::is_budgeted_v;
using ::crucible::fixy::is::is_cipher_tier_v;
using ::crucible::safety::extract::is_cipher_tier_v;
using ::crucible::fixy::is::is_clock_source_v;
using ::crucible::safety::extract::is_clock_source_v;
using ::crucible::fixy::is::is_consistency_v;
using ::crucible::safety::extract::is_consistency_v;
using ::crucible::fixy::is::is_consumer_handle_v;
using ::crucible::safety::extract::is_consumer_handle_v;
using ::crucible::fixy::is::is_cpu_pinned_v;
using ::crucible::safety::extract::is_cpu_pinned_v;
using ::crucible::fixy::is::is_crash_v;
using ::crucible::safety::extract::is_crash_v;
using ::crucible::fixy::is::is_det_safe_v;
using ::crucible::safety::extract::is_det_safe_v;
using ::crucible::fixy::is::is_epoch_versioned_v;
using ::crucible::safety::extract::is_epoch_versioned_v;
using ::crucible::fixy::is::is_hot_path_v;
using ::crucible::safety::extract::is_hot_path_v;
using ::crucible::fixy::is::is_hw_v;
using ::crucible::safety::extract::is_hw_v;
using ::crucible::fixy::is::is_join_policy_v;
using ::crucible::safety::extract::is_join_policy_v;
using ::crucible::fixy::is::is_linear_v;
using ::crucible::safety::extract::is_linear_v;
using ::crucible::fixy::is::is_mem_order_v;
using ::crucible::safety::extract::is_mem_order_v;
using ::crucible::fixy::is::is_numa_placement_v;
using ::crucible::safety::extract::is_numa_placement_v;
using ::crucible::fixy::is::is_numerical_tier_v;
using ::crucible::safety::extract::is_numerical_tier_v;
using ::crucible::fixy::is::is_opaque_lifetime_v;
using ::crucible::safety::extract::is_opaque_lifetime_v;
using ::crucible::fixy::is::is_owned_mmap_v;
using ::crucible::safety::extract::is_owned_mmap_v;
using ::crucible::fixy::is::is_owned_region_v;
using ::crucible::safety::extract::is_owned_region_v;
using ::crucible::fixy::is::is_permission_v;
using ::crucible::safety::extract::is_permission_v;
using ::crucible::fixy::is::is_producer_handle_v;
using ::crucible::safety::extract::is_producer_handle_v;
using ::crucible::fixy::is::is_progress_v;
using ::crucible::safety::extract::is_progress_v;
using ::crucible::fixy::is::is_recipe_spec_v;
using ::crucible::safety::extract::is_recipe_spec_v;
using ::crucible::fixy::is::is_reduce_into_v;
using ::crucible::safety::extract::is_reduce_into_v;
using ::crucible::fixy::is::is_refined_v;
using ::crucible::safety::extract::is_refined_v;
using ::crucible::fixy::is::is_residency_heat_v;
using ::crucible::safety::extract::is_residency_heat_v;
using ::crucible::fixy::is::is_sched_class_v;
using ::crucible::safety::extract::is_sched_class_v;
using ::crucible::fixy::is::is_scoped_fence_v;
using ::crucible::safety::extract::is_scoped_fence_v;
using ::crucible::fixy::is::is_secret_v;
using ::crucible::safety::extract::is_secret_v;
using ::crucible::fixy::is::is_session_handle_v;
using ::crucible::safety::extract::is_session_handle_v;
using ::crucible::fixy::is::is_shared_permission_v;
using ::crucible::safety::extract::is_shared_permission_v;
using ::crucible::fixy::is::is_simd_width_pinned_v;
using ::crucible::safety::extract::is_simd_width_pinned_v;
using ::crucible::fixy::is::is_stale_v;
using ::crucible::safety::extract::is_stale_v;
using ::crucible::fixy::is::is_suspend_behavior_v;
using ::crucible::safety::extract::is_suspend_behavior_v;
using ::crucible::fixy::is::is_swmr_reader_v;
using ::crucible::safety::extract::is_swmr_reader_v;
using ::crucible::fixy::is::is_swmr_writer_v;
using ::crucible::safety::extract::is_swmr_writer_v;
using ::crucible::fixy::is::is_tagged_v;
using ::crucible::safety::extract::is_tagged_v;
using ::crucible::fixy::is::is_vendor_v;
using ::crucible::safety::extract::is_vendor_v;
using ::crucible::fixy::is::is_wait_v;
using ::crucible::safety::extract::is_wait_v;
using ::crucible::fixy::is::join_policy_value_t;
using ::crucible::safety::extract::join_policy_value_t;
using ::crucible::fixy::is::linear_value_t;
using ::crucible::safety::extract::linear_value_t;
using ::crucible::fixy::is::mem_order_value_t;
using ::crucible::safety::extract::mem_order_value_t;
using ::crucible::fixy::is::numa_placement_value_t;
using ::crucible::safety::extract::numa_placement_value_t;
using ::crucible::fixy::is::numerical_tier_value_t;
using ::crucible::safety::extract::numerical_tier_value_t;
using ::crucible::fixy::is::opaque_lifetime_value_t;
using ::crucible::safety::extract::opaque_lifetime_value_t;
using ::crucible::fixy::is::owned_region_tag_t;
using ::crucible::safety::extract::owned_region_tag_t;
using ::crucible::fixy::is::owned_region_value_t;
using ::crucible::safety::extract::owned_region_value_t;
using ::crucible::fixy::is::permission_tag_t;
using ::crucible::safety::extract::permission_tag_t;
using ::crucible::fixy::is::producer_handle_value_t;
using ::crucible::safety::extract::producer_handle_value_t;
using ::crucible::fixy::is::progress_value_t;
using ::crucible::safety::extract::progress_value_t;
using ::crucible::fixy::is::recipe_spec_value_t;
using ::crucible::safety::extract::recipe_spec_value_t;
using ::crucible::fixy::is::reduce_into_accumulator_t;
using ::crucible::safety::extract::reduce_into_accumulator_t;
using ::crucible::fixy::is::reduce_into_reducer_t;
using ::crucible::safety::extract::reduce_into_reducer_t;
using ::crucible::fixy::is::refined_is_sealed_v;
using ::crucible::safety::extract::refined_is_sealed_v;
using ::crucible::fixy::is::refined_predicate_type_t;
using ::crucible::safety::extract::refined_predicate_type_t;
using ::crucible::fixy::is::refined_value_t;
using ::crucible::safety::extract::refined_value_t;
using ::crucible::fixy::is::residency_heat_value_t;
using ::crucible::safety::extract::residency_heat_value_t;
using ::crucible::fixy::is::sched_class_value_t;
using ::crucible::safety::extract::sched_class_value_t;
using ::crucible::fixy::is::scoped_fence_value_t;
using ::crucible::safety::extract::scoped_fence_value_t;
using ::crucible::fixy::is::secret_value_t;
using ::crucible::safety::extract::secret_value_t;
using ::crucible::fixy::is::session_handle_proto_t;
using ::crucible::safety::extract::session_handle_proto_t;
using ::crucible::fixy::is::shared_permission_tag_t;
using ::crucible::safety::extract::shared_permission_tag_t;
using ::crucible::fixy::is::simd_width_pinned_value_t;
using ::crucible::safety::extract::simd_width_pinned_value_t;
using ::crucible::fixy::is::stale_semiring_t;
using ::crucible::safety::extract::stale_semiring_t;
using ::crucible::fixy::is::stale_staleness_t;
using ::crucible::safety::extract::stale_staleness_t;
using ::crucible::fixy::is::stale_value_t;
using ::crucible::safety::extract::stale_value_t;
using ::crucible::fixy::is::suspend_behavior_value_t;
using ::crucible::safety::extract::suspend_behavior_value_t;
using ::crucible::fixy::is::swmr_reader_value_t;
using ::crucible::safety::extract::swmr_reader_value_t;
using ::crucible::fixy::is::swmr_writer_value_t;
using ::crucible::safety::extract::swmr_writer_value_t;
using ::crucible::fixy::is::tagged_tag_t;
using ::crucible::safety::extract::tagged_tag_t;
using ::crucible::fixy::is::tagged_value_t;
using ::crucible::safety::extract::tagged_value_t;
using ::crucible::fixy::is::vendor_value_t;
using ::crucible::safety::extract::vendor_value_t;
using ::crucible::fixy::is::wait_value_t;
using ::crucible::safety::extract::wait_value_t;
}  // namespace isx_alias_reach

int main() { return 0; }
