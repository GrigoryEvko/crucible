#pragma once

// The namespace carries a trailing underscore because struct is a
// keyword.

#include <crucible/Saturate.h>
#include <crucible/safety/Checked.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/_Pinned.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Workload.h>

#include <type_traits>

namespace crucible::fixy::struct_ {

using ::crucible::safety::NonMovable;
using ::crucible::safety::Pinned;

using ::crucible::safety::FinalBy;
using ::crucible::safety::NotInherited;
using ::crucible::safety::assert_not_inherited;

using ::crucible::safety::checked_abs;
using ::crucible::safety::checked_add;
using ::crucible::safety::checked_div;
using ::crucible::safety::checked_mod;
using ::crucible::safety::checked_mul;
using ::crucible::safety::checked_neg;
using ::crucible::safety::checked_shl;
using ::crucible::safety::checked_shr;
using ::crucible::safety::checked_sub;

using ::crucible::safety::wrapping_add;
using ::crucible::safety::wrapping_mul;
using ::crucible::safety::wrapping_sub;

using ::crucible::safety::trapping_add;
using ::crucible::safety::trapping_div;
using ::crucible::safety::trapping_mul;
using ::crucible::safety::trapping_sub;

using ::crucible::safety::saturating_add;
using ::crucible::safety::saturating_mul;
using ::crucible::safety::saturating_sub;

// The saturating_ names above and the _sat names below are the same
// three operations at two layers. Both spellings are re-exported so a
// caller keeps whichever layer it already speaks.
using ::crucible::sat::add_sat;
using ::crucible::sat::mul_sat;
using ::crucible::sat::sub_sat;

using ::crucible::safety::bytes_fit_v;
using ::crucible::safety::ensure_bytes_fit;
using ::crucible::safety::safe_add;
using ::crucible::safety::safe_add_all;
using ::crucible::safety::safe_array_bytes;
using ::crucible::safety::safe_byte_budget;
using ::crucible::safety::safe_capacity;
using ::crucible::safety::safe_mul;
using ::crucible::safety::safe_size_diff;
using ::crucible::safety::safe_size_sum;
using ::crucible::safety::safe_struct_bytes;
using ::crucible::safety::safe_sub;

namespace ct {

using ::crucible::safety::ct::cswap;
using ::crucible::safety::ct::eq;
using ::crucible::safety::ct::is_zero;
using ::crucible::safety::ct::less;
using ::crucible::safety::ct::mask_from_bit;
using ::crucible::safety::ct::select;

}  // namespace ct

namespace simd {

using ::crucible::simd::i32x16;
using ::crucible::simd::i32x8;
using ::crucible::simd::i64x4;
using ::crucible::simd::i64x8;
using ::crucible::simd::i64x8_mask;
using ::crucible::simd::u32x16;
using ::crucible::simd::u32x8;
using ::crucible::simd::u32x8_mask;
using ::crucible::simd::u64x4;
using ::crucible::simd::u64x8;
using ::crucible::simd::u64x8_mask;
using ::crucible::simd::u8x16;
using ::crucible::simd::u8x32;

template <typename V>
concept DetSafeSimd = ::crucible::simd::DetSafeSimd<V>;

// Neither primitive has a standard-library counterpart.
using ::crucible::simd::iota_v;
using ::crucible::simd::prefix_mask;

using ::crucible::simd::kAvx2Available;
using ::crucible::simd::kAvx512Available;
using ::crucible::simd::kNeonAvailable;
using ::crucible::simd::kSse42Available;

using ::crucible::simd::runtime_supports_avx2;
using ::crucible::simd::runtime_supports_avx512;
using ::crucible::simd::runtime_supports_sse42;

}  // namespace simd

using ::crucible::safety::OwnedRegion;
using ::crucible::safety::Slice;

using ::crucible::safety::log_topology_at_startup;
using ::crucible::safety::parallel_apply_pair;
using ::crucible::safety::parallel_for_smart;
using ::crucible::safety::parallel_for_views;
using ::crucible::safety::parallel_for_views_adaptive;
using ::crucible::safety::parallel_reduce_views;
using ::crucible::safety::should_parallelize;
using ::crucible::safety::WorkBudget;

}  // namespace crucible::fixy::struct_

namespace crucible::fixy::struct_::self_test {

static_assert(std::is_same_v<::crucible::fixy::struct_::Pinned<int>, ::crucible::safety::Pinned<int>>,
              "Pinned must alias the substrate template.");

static_assert(std::is_same_v<::crucible::fixy::struct_::NonMovable<int>, ::crucible::safety::NonMovable<int>>,
              "NonMovable must alias the substrate template.");

static_assert(std::is_same_v<::crucible::fixy::struct_::FinalBy<int>, ::crucible::safety::FinalBy<int>>,
              "FinalBy must alias the substrate template.");

static_assert(
    std::is_same_v<::crucible::fixy::struct_::OwnedRegion<int, void>, ::crucible::safety::OwnedRegion<int, void>>,
    "OwnedRegion must alias the substrate template.");

static_assert(std::is_same_v<::crucible::fixy::struct_::WorkBudget, ::crucible::safety::WorkBudget>,
              "WorkBudget must alias the substrate type.");

struct NotInheritedProbe_ final {};
static_assert(::crucible::fixy::struct_::NotInherited<NotInheritedProbe_>);
static_assert(::crucible::fixy::struct_::NotInherited<NotInheritedProbe_>
                  == ::crucible::safety::NotInherited<NotInheritedProbe_>,
              "NotInherited must mirror the substrate concept.");

constexpr int struct_outer_cardinality = 77;
constexpr int struct_ct_cardinality = 6;
constexpr int struct_simd_using_cardinality = 22;
constexpr int struct_simd_concept_cardinality = 1;

static_assert(struct_outer_cardinality == 77, "The outer re-export count and the using-declarations above "
                                              "must move together.");
static_assert(struct_ct_cardinality == 6, "The constant-time re-export count has drifted.");
static_assert(struct_simd_using_cardinality == 22, "The SIMD re-export count has drifted.");
static_assert(struct_simd_concept_cardinality == 1, "The SIMD namespace re-exports one concept.");

}  // namespace crucible::fixy::struct_::self_test
