#pragma once

// One nesting order over the wrappers below is canonical, outermost
// first, and the positions assigned here are that order. It is
// load-bearing: nesting is order-sensitive, and the row hash folds along
// the stack, so a stack built in another order compiles cleanly and then
// addresses a different cache slot than every peer that built it
// canonically.
//
// The predicate here is an opt-in gate, not a global rule. A site that
// requires canonical order constrains on the concept and rejects an
// out-of-order stack where it is written, instead of leaving it to
// surface later as a cache miss. A site that wants a deliberately
// different slot simply does not constrain.

#include <fixy/Bands.h>
#include <fixy/Qtt.h>
#include <fixy/Refined.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>

#include <concepts>
#include <cstdint>
#include <tuple>
#include <type_traits>

namespace fixy::canonical_order {

// One tag per canonical layer.  The tuple below lists them outermost
// first, and it is the one place that states the order: the position of
// a layer and the count of layers are both read from it.
namespace layer {
struct hot_path {};
struct det_safe {};
struct numerical_tier {};
struct vendor {};
struct residency_heat {};
struct cipher_tier {};
struct alloc_class {};
struct wait {};
struct stale {};
struct tagged {};
struct refined {};
struct secret {};
struct linear {};
struct computation {};
}  // namespace layer

using canonical_layers =
    std::tuple<layer::hot_path, layer::det_safe, layer::numerical_tier, layer::vendor, layer::residency_heat,
               layer::cipher_tier, layer::alloc_class, layer::wait, layer::stale, layer::tagged, layer::refined,
               layer::secret, layer::linear, layer::computation>;

inline constexpr int kCanonicalLayerCount = static_cast<int>(std::tuple_size_v<canonical_layers>);

namespace detail {

// The index of Layer in the tuple, or -1 when the tuple does not list it.
template <typename Layer, typename Order>
struct layer_position;

template <typename Layer, typename... Layers>
struct layer_position<Layer, std::tuple<Layers...>> {
    static consteval int find() noexcept {
        constexpr bool is_match[] = {std::is_same_v<Layer, Layers>...};
        for (int index = 0; index < static_cast<int>(sizeof...(Layers)); ++index) {
            if (is_match[index]) return index;
        }
        return -1;
    }
    static constexpr int value = find();
};

}  // namespace detail

template <typename Layer>
inline constexpr int layer_position_v = detail::layer_position<Layer, canonical_layers>::value;

// The primary template stays undefined. A canonical wrapper specializes
// it with its position. Every other wrapper deliberately does not, which
// is what makes the walk step over it without judging the order.

template <typename W>
struct canonical_layer_index;

template <auto Tier, typename T>
struct canonical_layer_index<::fixy::HotPath<Tier, T>> {
    static constexpr int value = layer_position_v<layer::hot_path>;
};

template <auto Tier, typename T>
struct canonical_layer_index<::fixy::DetSafe<Tier, T>> {
    static constexpr int value = layer_position_v<layer::det_safe>;
};

template <auto Tier, typename T>
struct canonical_layer_index<::fixy::NumericalTier<Tier, T>> {
    static constexpr int value = layer_position_v<layer::numerical_tier>;
};

template <auto Backend, typename T>
struct canonical_layer_index<::fixy::Vendor<Backend, T>> {
    static constexpr int value = layer_position_v<layer::vendor>;
};

template <auto Tier, typename T>
struct canonical_layer_index<::fixy::ResidencyHeat<Tier, T>> {
    static constexpr int value = layer_position_v<layer::residency_heat>;
};

template <auto Tier, typename T>
struct canonical_layer_index<::fixy::CipherTier<Tier, T>> {
    static constexpr int value = layer_position_v<layer::cipher_tier>;
};

template <auto Tag, typename T>
struct canonical_layer_index<::fixy::AllocClass<Tag, T>> {
    static constexpr int value = layer_position_v<layer::alloc_class>;
};

template <auto Strategy, typename T>
struct canonical_layer_index<::fixy::Wait<Strategy, T>> {
    static constexpr int value = layer_position_v<layer::wait>;
};

template <typename T>
struct canonical_layer_index<::fixy::Stale<T>> {
    static constexpr int value = layer_position_v<layer::stale>;
};

template <typename T, typename Tag>
struct canonical_layer_index<::fixy::Tagged<T, Tag>> {
    static constexpr int value = layer_position_v<layer::tagged>;
};

template <auto Pred, typename T>
struct canonical_layer_index<::fixy::Refined<Pred, T>> {
    static constexpr int value = layer_position_v<layer::refined>;
};

template <typename T>
struct canonical_layer_index<::fixy::Secret<T>> {
    static constexpr int value = layer_position_v<layer::secret>;
};

template <typename T>
struct canonical_layer_index<::fixy::Linear<T>> {
    static constexpr int value = layer_position_v<layer::linear>;
};

template <typename Row, typename T>
struct canonical_layer_index<::foundation::effects::Computation<Row, T>> {
    static constexpr int value = layer_position_v<layer::computation>;
};

template <typename W>
concept HasCanonicalLayerIndex = requires {
    { canonical_layer_index<std::remove_cvref_t<W>>::value } -> std::convertible_to<int>;
};

template <typename W>
    requires HasCanonicalLayerIndex<W>
inline constexpr int canonical_layer_index_v = canonical_layer_index<std::remove_cvref_t<W>>::value;

// The walk descends inward and accepts only a strictly increasing run of
// canonical positions. Strict, because the same wrapper twice in one
// stack is a defect rather than a reordering.

namespace detail {

template <int Prev, typename Layer>
struct walk_canonical {
    static consteval bool eval() noexcept {
        using L = std::remove_cvref_t<Layer>;
        if constexpr (HasCanonicalLayerIndex<L>) {
            constexpr int my = canonical_layer_index<L>::value;
            if (my <= Prev) return false;
            if constexpr (requires { typename L::value_type; }) {
                return walk_canonical<my, typename L::value_type>::eval();
            } else {
                return true;
            }
        } else {
            // A wrapper with no position, or the payload. Carry Prev
            // through so it neither advances nor breaks the run.
            if constexpr (requires { typename L::value_type; }) {
                return walk_canonical<Prev, typename L::value_type>::eval();
            } else {
                return true;
            }
        }
    }
};

}  // namespace detail

template <typename Stack>
[[nodiscard]] consteval bool is_canonically_ordered() noexcept {
    return detail::walk_canonical<-1, std::remove_cvref_t<Stack>>::eval();
}

template <typename Stack>
inline constexpr bool is_canonically_ordered_v = is_canonically_ordered<Stack>();

template <typename Stack>
concept CanonicallyOrdered = is_canonically_ordered_v<Stack>;

// These pin the positions at the point of declaration. Every consumer
// has to see the same numbers, so a renumbering changes the
// specializations above and every reader of a position together.

namespace detail::canonical_order_self_test {

using ::fixy::AllocClassTag_v;
using ::fixy::CipherTierTag_v;
using ::fixy::DetSafeTier_v;
using ::fixy::HotPathTier_v;
using ::fixy::ResidencyHeatTag_v;
using ::fixy::Tolerance;
using ::fixy::VendorBackend_v;
using ::fixy::WaitStrategy_v;
using FromUser = ::fixy::tags::source::FromUser;
using BoundedInt = ::fixy::Refined<::fixy::bounded_above<int{8}>, int>;
using PureComputation = ::foundation::effects::Computation<::foundation::effects::Row<>, int>;

static_assert(canonical_layer_index_v<::fixy::HotPath<HotPathTier_v::Hot, int>> == 0);
static_assert(canonical_layer_index_v<::fixy::DetSafe<DetSafeTier_v::Pure, int>> == 1);
static_assert(canonical_layer_index_v<::fixy::NumericalTier<Tolerance::BITEXACT, int>> == 2);
static_assert(canonical_layer_index_v<::fixy::Vendor<VendorBackend_v::NV, int>> == 3);
static_assert(canonical_layer_index_v<::fixy::ResidencyHeat<ResidencyHeatTag_v::Hot, int>> == 4);
static_assert(canonical_layer_index_v<::fixy::CipherTier<CipherTierTag_v::Hot, int>> == 5);
static_assert(canonical_layer_index_v<::fixy::AllocClass<AllocClassTag_v::Arena, int>> == 6);
static_assert(canonical_layer_index_v<::fixy::Wait<WaitStrategy_v::SpinPause, int>> == 7);
static_assert(canonical_layer_index_v<::fixy::Stale<int>> == 8);
static_assert(canonical_layer_index_v<::fixy::Tagged<int, FromUser>> == 9);
static_assert(canonical_layer_index_v<BoundedInt> == 10);
static_assert(canonical_layer_index_v<::fixy::Secret<int>> == 11);
static_assert(canonical_layer_index_v<::fixy::Linear<int>> == 12);
static_assert(canonical_layer_index_v<PureComputation> == 13);

static_assert(kCanonicalLayerCount == 14,
              "the canonical wrapper-nesting order no longer holds 14 positions; the positions pinned "
              "above and every stack written against them have to move together");

static_assert(is_canonically_ordered_v<int>);
static_assert(is_canonically_ordered_v<double>);

static_assert(is_canonically_ordered_v<::fixy::Linear<int>>);
static_assert(is_canonically_ordered_v<::fixy::HotPath<HotPathTier_v::Hot, int>>);

static_assert(is_canonically_ordered_v<::fixy::HotPath<HotPathTier_v::Hot, ::fixy::Linear<int>>>);

static_assert(!is_canonically_ordered_v<::fixy::Linear<::fixy::HotPath<HotPathTier_v::Hot, int>>>);

// One wrapper twice in one stack is rejected, not merely tolerated.
static_assert(
    !is_canonically_ordered_v<::fixy::HotPath<HotPathTier_v::Hot, ::fixy::HotPath<HotPathTier_v::Cold, int>>>);

static_assert(
    is_canonically_ordered_v<
        ::fixy::HotPath<HotPathTier_v::Hot,
                        ::fixy::DetSafe<DetSafeTier_v::Pure,
                                        ::fixy::NumericalTier<Tolerance::BITEXACT,
                                                              ::fixy::Vendor<VendorBackend_v::NV, PureComputation>>>>>);

static_assert(is_canonically_ordered_v<::fixy::Tagged<BoundedInt, FromUser>>);

static_assert(is_canonically_ordered_v<::fixy::Stale<::fixy::Tagged<BoundedInt, FromUser>>>);

static_assert(!is_canonically_ordered_v<
              ::fixy::Refined<::fixy::bounded_above<int{8}>, ::fixy::Tagged<::fixy::Stale<int>, FromUser>>>);

}  // namespace detail::canonical_order_self_test
}  // namespace fixy::canonical_order
