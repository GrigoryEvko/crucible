// The compile-time checks of fixy/CanonicalOrder.h.

#include <fixy/CanonicalOrder.h>

namespace fixy::canonical_order {

// These pin the positions at the point of declaration. Every consumer
// has to see the same numbers, so a renumbering changes the
// specializations above and every reader of a position together.

namespace detail::canonical_order_self_test {

using ::fixy::AllocClassTag_v;
using ::fixy::CipherTierTag_v;
using ::fixy::DetSafeTier_v;
using ::fixy::ResidencyHeatTag_v;
using ::fixy::Tolerance;
using ::fixy::VendorBackend_v;
using ::fixy::WaitStrategy_v;
using FromUser = ::fixy::tags::source::FromUser;
using BoundedInt = ::fixy::Refined<::fixy::bounded_above<int{8}>, int>;
using PureComputation = ::foundation::effects::Computation<::foundation::effects::Row<>, int>;

static_assert(canonical_layer_index_v<::fixy::DetSafe<DetSafeTier_v::Pure, int>> == 0);
static_assert(canonical_layer_index_v<::fixy::NumericalTier<Tolerance::BITEXACT, int>> == 1);
static_assert(canonical_layer_index_v<::fixy::Vendor<VendorBackend_v::NV, int>> == 2);
static_assert(canonical_layer_index_v<::fixy::ResidencyHeat<ResidencyHeatTag_v::Hot, int>> == 3);
static_assert(canonical_layer_index_v<::fixy::CipherTier<CipherTierTag_v::Hot, int>> == 4);
static_assert(canonical_layer_index_v<::fixy::AllocClass<AllocClassTag_v::Arena, int>> == 5);
static_assert(canonical_layer_index_v<::fixy::Wait<WaitStrategy_v::SpinPause, int>> == 6);
static_assert(canonical_layer_index_v<::fixy::Stale<int>> == 7);
static_assert(canonical_layer_index_v<::fixy::Tagged<int, FromUser>> == 8);
static_assert(canonical_layer_index_v<BoundedInt> == 9);
static_assert(canonical_layer_index_v<::fixy::Secret<int>> == 10);
static_assert(canonical_layer_index_v<::fixy::Linear<int>> == 11);
static_assert(canonical_layer_index_v<PureComputation> == 12);

static_assert(kCanonicalLayerCount == 13,
              "the canonical wrapper-nesting order no longer holds 13 positions; the positions pinned "
              "above and every stack written against them have to move together");

static_assert(is_canonically_ordered_v<int>);
static_assert(is_canonically_ordered_v<double>);

static_assert(is_canonically_ordered_v<::fixy::Linear<int>>);
static_assert(is_canonically_ordered_v<::fixy::DetSafe<DetSafeTier_v::Pure, int>>);

static_assert(is_canonically_ordered_v<::fixy::DetSafe<DetSafeTier_v::Pure, ::fixy::Linear<int>>>);

static_assert(!is_canonically_ordered_v<::fixy::Linear<::fixy::DetSafe<DetSafeTier_v::Pure, int>>>);

// One wrapper twice in one stack is rejected, not merely tolerated.
static_assert(
    !is_canonically_ordered_v<::fixy::DetSafe<DetSafeTier_v::Pure, ::fixy::DetSafe<DetSafeTier_v::PhiloxRng, int>>>);

static_assert(is_canonically_ordered_v<::fixy::DetSafe<
                  DetSafeTier_v::Pure,
                  ::fixy::NumericalTier<Tolerance::BITEXACT,
                                        ::fixy::Vendor<VendorBackend_v::NV,
                                                       ::fixy::CipherTier<CipherTierTag_v::Hot, PureComputation>>>>>);

static_assert(is_canonically_ordered_v<::fixy::Tagged<BoundedInt, FromUser>>);

static_assert(is_canonically_ordered_v<::fixy::Stale<::fixy::Tagged<BoundedInt, FromUser>>>);

static_assert(!is_canonically_ordered_v<
              ::fixy::Refined<::fixy::bounded_above<int{8}>, ::fixy::Tagged<::fixy::Stale<int>, FromUser>>>);

}  // namespace detail::canonical_order_self_test
}  // namespace fixy::canonical_order
