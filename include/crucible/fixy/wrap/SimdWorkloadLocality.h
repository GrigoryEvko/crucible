#pragma once

#include <crucible/safety/Simd.h>
#include <crucible/safety/Workload.h>
#include <crucible/safety/LocalityHint.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::fixy::wrap {

using ::crucible::simd::i64x4;
using ::crucible::simd::i64x8;
using ::crucible::simd::u64x4;
using ::crucible::simd::u64x8;
using ::crucible::simd::i32x8;
using ::crucible::simd::i32x16;
using ::crucible::simd::u32x8;
using ::crucible::simd::u32x16;
using ::crucible::simd::u8x16;
using ::crucible::simd::u8x32;

using ::crucible::simd::i64x8_mask;
using ::crucible::simd::u64x8_mask;
using ::crucible::simd::u32x8_mask;

using ::crucible::simd::DetSafeSimd;

using ::crucible::simd::iota_v;

using ::crucible::simd::prefix_mask;

using ::crucible::simd::kAvx512Available;
using ::crucible::simd::kAvx2Available;
using ::crucible::simd::kSse42Available;
using ::crucible::simd::kNeonAvailable;

using ::crucible::simd::runtime_supports_avx512;
using ::crucible::simd::runtime_supports_avx2;
using ::crucible::simd::runtime_supports_sse42;

using ::crucible::safety::WorkBudget;

using ::crucible::safety::should_parallelize;

using ::crucible::safety::parallel_for_views;

using ::crucible::safety::parallel_reduce_views;

using ::crucible::safety::parallel_apply_pair;

using ::crucible::safety::parallel_for_views_adaptive;

using ::crucible::safety::parallel_for_smart;

using ::crucible::safety::log_topology_at_startup;

using ::crucible::safety::LocalityIgnore_t;
using ::crucible::safety::LocalityLocal_t;
using ::crucible::safety::LocalitySpread_t;

using ::crucible::safety::HasLocalityHint;

using ::crucible::safety::locality_hint_of_v;

using ::crucible::safety::recommend_parallelism_with_locality;

}  // namespace crucible::fixy::wrap

namespace crucible::fixy::wrap::self_test_simd_workload_locality {

static_assert(std::is_same_v<::crucible::fixy::wrap::i64x8, ::crucible::simd::i64x8>);
static_assert(std::is_same_v<::crucible::fixy::wrap::u32x8, ::crucible::simd::u32x8>);
static_assert(std::is_same_v<::crucible::fixy::wrap::u8x16, ::crucible::simd::u8x16>);
static_assert(std::is_same_v<::crucible::fixy::wrap::i64x8_mask, ::crucible::simd::i64x8_mask>);

static_assert(::crucible::fixy::wrap::kAvx512Available == ::crucible::simd::kAvx512Available);
static_assert(::crucible::fixy::wrap::kAvx2Available == ::crucible::simd::kAvx2Available);
static_assert(::crucible::fixy::wrap::kSse42Available == ::crucible::simd::kSse42Available);
static_assert(::crucible::fixy::wrap::kNeonAvailable == ::crucible::simd::kNeonAvailable);

static_assert(::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::i64x8>);
static_assert(::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::u64x8>);
static_assert(::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::u32x8>);
static_assert(!::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::vec<float, 8>>);
static_assert(::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::i64x8>
              == ::crucible::simd::DetSafeSimd<::crucible::simd::i64x8>);

[[nodiscard]] consteval bool iota_v_through_alias_matches_substrate() noexcept {
    constexpr auto via_fixy = ::crucible::fixy::wrap::iota_v<::crucible::simd::u64x8>();
    constexpr auto via_substrate = ::crucible::simd::iota_v<::crucible::simd::u64x8>();
    bool all_match = true;
    for (int lane = 0; lane < 8; ++lane) {
        if (via_fixy[lane] != via_substrate[lane]) {
            all_match = false;
        }
    }
    return all_match;
}
static_assert(iota_v_through_alias_matches_substrate());

static_assert(std::is_same_v<::crucible::fixy::wrap::WorkBudget, ::crucible::safety::WorkBudget>);

[[nodiscard]] consteval bool workbudget_default_state_preserved() noexcept {
    ::crucible::fixy::wrap::WorkBudget b{};
    return b.read_bytes == 0 && b.write_bytes == 0 && b.item_count == 0;
}
static_assert(workbudget_default_state_preserved());

struct WorkloadProbeTag {};

// The workload functions carry no identity assertion here. A using-declaration
// names the same declaration rather than a second entity, so a function-pointer
// comparison against the substrate is tautological and the tautological-compare
// diagnostic rejects it. Calling through the alias is the witness that remains,
// and it lives in the runtime test.

static_assert(std::is_same_v<::crucible::fixy::wrap::LocalityIgnore_t, ::crucible::safety::LocalityIgnore_t>);
static_assert(std::is_same_v<::crucible::fixy::wrap::LocalityLocal_t, ::crucible::safety::LocalityLocal_t>);
static_assert(std::is_same_v<::crucible::fixy::wrap::LocalitySpread_t, ::crucible::safety::LocalitySpread_t>);

static_assert(std::is_empty_v<::crucible::fixy::wrap::LocalityIgnore_t>);
static_assert(std::is_empty_v<::crucible::fixy::wrap::LocalityLocal_t>);
static_assert(std::is_empty_v<::crucible::fixy::wrap::LocalitySpread_t>);

struct LocalityHintProbe_Unhinted {};
struct LocalityHintProbe_Local {
    using locality_hint = ::crucible::safety::LocalityLocal_t;
};
struct LocalityHintProbe_Typo {
    using locality_hint = int;
};

static_assert(!::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Unhinted>);
static_assert(::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Local>);
static_assert(!::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Typo>);

static_assert(::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Local>
              == ::crucible::safety::HasLocalityHint<LocalityHintProbe_Local>);
static_assert(::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Typo>
              == ::crucible::safety::HasLocalityHint<LocalityHintProbe_Typo>);

static_assert(::crucible::fixy::wrap::locality_hint_of_v<LocalityHintProbe_Unhinted>
              == ::crucible::concurrent::NumaPolicy::NumaIgnore);
static_assert(::crucible::fixy::wrap::locality_hint_of_v<LocalityHintProbe_Local>
              == ::crucible::concurrent::NumaPolicy::NumaLocal);

static_assert(::crucible::fixy::wrap::locality_hint_of_v<LocalityHintProbe_Local>
              == ::crucible::safety::locality_hint_of_v<LocalityHintProbe_Local>);
static_assert(::crucible::fixy::wrap::locality_hint_of_v<LocalityHintProbe_Unhinted>
              == ::crucible::safety::locality_hint_of_v<LocalityHintProbe_Unhinted>);

constexpr int simd_workload_locality_alias_cardinality = 37;
static_assert(simd_workload_locality_alias_cardinality == 37,
              "fixy::wrap::{Simd,Workload,LocalityHint} cardinality changed "
              "— update the using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::wrap::self_test_simd_workload_locality
