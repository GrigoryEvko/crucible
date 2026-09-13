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

#include <crucible/safety/AllocClass.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/effects/Computation.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace crucible::safety::diag::canonical_order {

// The primary template stays undefined. A canonical wrapper specializes
// it with its position. Every other wrapper deliberately does not, which
// is what makes the walk step over it without judging the order.

template <typename W>
struct canonical_layer_index;

template <auto Tier, typename T>
struct canonical_layer_index<HotPath<Tier, T>> {
    static constexpr int value = 0;
};

template <auto Tier, typename T>
struct canonical_layer_index<DetSafe<Tier, T>> {
    static constexpr int value = 1;
};

template <auto Tier, typename T>
struct canonical_layer_index<NumericalTier<Tier, T>> {
    static constexpr int value = 2;
};

template <auto Backend, typename T>
struct canonical_layer_index<Vendor<Backend, T>> {
    static constexpr int value = 3;
};

template <auto Tier, typename T>
struct canonical_layer_index<ResidencyHeat<Tier, T>> {
    static constexpr int value = 4;
};

template <auto Tier, typename T>
struct canonical_layer_index<CipherTier<Tier, T>> {
    static constexpr int value = 5;
};

template <auto Tag, typename T>
struct canonical_layer_index<AllocClass<Tag, T>> {
    static constexpr int value = 6;
};

template <auto Strategy, typename T>
struct canonical_layer_index<Wait<Strategy, T>> {
    static constexpr int value = 7;
};

template <auto Tag, typename T>
struct canonical_layer_index<MemOrder<Tag, T>> {
    static constexpr int value = 8;
};

template <auto Class, typename T>
struct canonical_layer_index<Progress<Class, T>> {
    static constexpr int value = 9;
};

template <typename T>
struct canonical_layer_index<Stale<T>> {
    static constexpr int value = 10;
};

template <typename T, typename Tag>
struct canonical_layer_index<Tagged<T, Tag>> {
    static constexpr int value = 11;
};

template <auto Pred, typename T>
struct canonical_layer_index<Refined<Pred, T>> {
    static constexpr int value = 12;
};

template <typename T>
struct canonical_layer_index<Secret<T>> {
    static constexpr int value = 13;
};

template <typename T>
struct canonical_layer_index<Linear<T>> {
    static constexpr int value = 14;
};

template <typename Row, typename T>
struct canonical_layer_index<::crucible::effects::Computation<Row, T>> {
    static constexpr int value = 15;
};

inline constexpr int kCanonicalLayerCount = 16;

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

namespace _selftest {

static_assert(canonical_layer_index_v<HotPath<HotPathTier_v::Hot, int>> == 0);
static_assert(canonical_layer_index_v<DetSafe<DetSafeTier_v::Pure, int>> == 1);
static_assert(canonical_layer_index_v<NumericalTier<::crucible::algebra::lattices::Tolerance::BITEXACT, int>> == 2);
static_assert(canonical_layer_index_v<Vendor<VendorBackend_v::NV, int>> == 3);
static_assert(canonical_layer_index_v<ResidencyHeat<ResidencyHeatTag_v::Hot, int>> == 4);
static_assert(canonical_layer_index_v<CipherTier<CipherTierTag_v::Hot, int>> == 5);
static_assert(canonical_layer_index_v<AllocClass<AllocClassTag_v::Arena, int>> == 6);
static_assert(canonical_layer_index_v<Wait<WaitStrategy_v::SpinPause, int>> == 7);
static_assert(canonical_layer_index_v<MemOrder<MemOrderTag_v::SeqCst, int>> == 8);
static_assert(canonical_layer_index_v<Progress<ProgressClass_v::Bounded, int>> == 9);
static_assert(canonical_layer_index_v<Stale<int>> == 10);
static_assert(canonical_layer_index_v<Tagged<int, source::FromUser>> == 11);
static_assert(canonical_layer_index_v<Refined<bounded_above<int{8}>, int>> == 12);
static_assert(canonical_layer_index_v<Secret<int>> == 13);
static_assert(canonical_layer_index_v<Linear<int>> == 14);
static_assert(canonical_layer_index_v<::crucible::effects::Computation<::crucible::effects::Row<>, int>> == 15);

static_assert(kCanonicalLayerCount == 16,
              "the canonical wrapper-nesting order no longer holds 16 positions; the count and the "
              "specializations above have to move together");

static_assert(is_canonically_ordered_v<int>);
static_assert(is_canonically_ordered_v<double>);

static_assert(is_canonically_ordered_v<Linear<int>>);
static_assert(is_canonically_ordered_v<HotPath<HotPathTier_v::Hot, int>>);

static_assert(is_canonically_ordered_v<HotPath<HotPathTier_v::Hot, Linear<int>>>);

static_assert(!is_canonically_ordered_v<Linear<HotPath<HotPathTier_v::Hot, int>>>);

// One wrapper twice in one stack is rejected, not merely tolerated.
static_assert(!is_canonically_ordered_v<HotPath<HotPathTier_v::Hot, HotPath<HotPathTier_v::Cold, int>>>);

static_assert(
    is_canonically_ordered_v<
        HotPath<HotPathTier_v::Hot,
                DetSafe<DetSafeTier_v::Pure,
                        NumericalTier<::crucible::algebra::lattices::Tolerance::BITEXACT,
                                      Vendor<VendorBackend_v::NV,
                                             ::crucible::effects::Computation<::crucible::effects::Row<>, int>>>>>>);

static_assert(is_canonically_ordered_v<Tagged<Refined<bounded_above<int{8}>, int>, source::FromUser>>);

static_assert(is_canonically_ordered_v<Stale<Tagged<Refined<bounded_above<int{8}>, int>, source::FromUser>>>);

static_assert(!is_canonically_ordered_v<Refined<bounded_above<int{8}>, Tagged<Stale<int>, source::FromUser>>>);

}  // namespace _selftest
}  // namespace crucible::safety::diag::canonical_order
