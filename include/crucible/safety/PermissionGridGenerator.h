#pragma once

// Splits one region's permission across a grid of M producers and N
// consumers.
//
// The split happens in two levels: first a binary split into a
// producer side and a consumer side, then an ordinary one-dimensional
// split within each side.  A single flat specialization taking both
// index packs at once is not deducible, because two unrelated packs
// cannot both be deduced across one class-template boundary.
//
// So the grid is conceptual.  What the permission system sees is M
// plus N disjoint sub-regions, told apart by which side they came
// from, and no new specialization is needed to describe them.

#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionTreeGenerator.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename Whole>
struct ProducerSide {
    using parent_type = Whole;
};

template <typename Whole>
struct ConsumerSide {
    using parent_type = Whole;
};

template <typename Whole>
struct splits_into<Whole, ProducerSide<Whole>, ConsumerSide<Whole>> : std::true_type {};

template <typename Whole>
struct splits_into_authoring_witness<Whole, ProducerSide<Whole>, ConsumerSide<Whole>> : std::true_type {};

template <typename Whole, std::size_t I>
using Producer = Slice<ProducerSide<Whole>, I>;

template <typename Whole, std::size_t J>
using Consumer = Slice<ConsumerSide<Whole>, J>;

template <typename Whole, std::size_t M, std::size_t N>
struct auto_split_grid {
    static_assert(M > 0, "auto_split_grid<Whole, M, N>: M (producer count) must be > 0");
    static_assert(N > 0, "auto_split_grid<Whole, M, N>: N (consumer count) must be > 0");

    using whole_type = Whole;
    using producer_side_type = ProducerSide<Whole>;
    using consumer_side_type = ConsumerSide<Whole>;

    static constexpr std::size_t producer_count = M;
    static constexpr std::size_t consumer_count = N;

    using producer_tags = auto_split_n_t<ProducerSide<Whole>, M>;
    using consumer_tags = auto_split_n_t<ConsumerSide<Whole>, N>;

    using producer_perms = auto_split_n_permissions_t<ProducerSide<Whole>, M>;
    using consumer_perms = auto_split_n_permissions_t<ConsumerSide<Whole>, N>;
};

// Deliberately an aggregate, so designated initialization works.  It
// is move-only, because each permission it holds is.
template <typename Whole, std::size_t M, std::size_t N>
struct [[nodiscard]] GridPermissions {
    static_assert(M > 0 && N > 0);

    typename auto_split_grid<Whole, M, N>::producer_perms producers;
    typename auto_split_grid<Whole, M, N>::consumer_perms consumers;
};

// One value the factory's requires-clause can name, folding both
// counts and both levels of the split into a single check.
template <typename Whole, std::size_t M, std::size_t N>
inline constexpr bool can_split_grid_v =
    (M > 0) && (N > 0) && splits_into_v<Whole, ProducerSide<Whole>, ConsumerSide<Whole>>
    && can_split_n_v<ProducerSide<Whole>, M> && can_split_n_v<ConsumerSide<Whole>, N>;

namespace detail {

template <typename Whole, std::size_t... Is>
[[nodiscard]] constexpr auto split_producer_side_(Permission<ProducerSide<Whole>>&& side,
                                                  std::index_sequence<Is...>) noexcept {
    return mint_permission_split_n<Slice<ProducerSide<Whole>, Is>...>(std::move(side));
}

template <typename Whole, std::size_t... Js>
[[nodiscard]] constexpr auto split_consumer_side_(Permission<ConsumerSide<Whole>>&& side,
                                                  std::index_sequence<Js...>) noexcept {
    return mint_permission_split_n<Slice<ConsumerSide<Whole>, Js>...>(std::move(side));
}

}  // namespace detail

template <typename Whole, std::size_t M, std::size_t N>
    requires can_split_grid_v<Whole, M, N>
[[nodiscard]] constexpr auto mint_grid_permissions(Permission<Whole>&& parent) noexcept
    -> GridPermissions<Whole, M, N> {
    auto sides = mint_permission_split<ProducerSide<Whole>, ConsumerSide<Whole>>(std::move(parent));

    return GridPermissions<Whole, M, N>{
        .producers = detail::split_producer_side_<Whole>(std::move(sides.first), std::make_index_sequence<M>{}),
        .consumers = detail::split_consumer_side_<Whole>(std::move(sides.second), std::make_index_sequence<N>{}),
    };
}

namespace detail {
struct grid_test_tag_ {};
}  // namespace detail

static_assert(sizeof(ProducerSide<detail::grid_test_tag_>) == 1);
static_assert(sizeof(ConsumerSide<detail::grid_test_tag_>) == 1);
static_assert(std::is_empty_v<ProducerSide<detail::grid_test_tag_>>);
static_assert(std::is_empty_v<ConsumerSide<detail::grid_test_tag_>>);

static_assert(std::is_same_v<Producer<detail::grid_test_tag_, 0>, Slice<ProducerSide<detail::grid_test_tag_>, 0>>);
static_assert(std::is_same_v<Consumer<detail::grid_test_tag_, 0>, Slice<ConsumerSide<detail::grid_test_tag_>, 0>>);

static_assert(!std::is_same_v<Producer<detail::grid_test_tag_, 0>, Consumer<detail::grid_test_tag_, 0>>);

static_assert(
    splits_into_v<detail::grid_test_tag_, ProducerSide<detail::grid_test_tag_>, ConsumerSide<detail::grid_test_tag_>>);

static_assert(can_split_grid_v<detail::grid_test_tag_, 1, 1>);
static_assert(can_split_grid_v<detail::grid_test_tag_, 4, 4>);
static_assert(can_split_grid_v<detail::grid_test_tag_, 8, 16>);
static_assert(!can_split_grid_v<detail::grid_test_tag_, 0, 4>);
static_assert(!can_split_grid_v<detail::grid_test_tag_, 4, 0>);

static_assert(std::is_same_v<auto_split_grid<detail::grid_test_tag_, 2, 3>::producer_tags,
                             std::tuple<Producer<detail::grid_test_tag_, 0>, Producer<detail::grid_test_tag_, 1>>>);
static_assert(std::is_same_v<auto_split_grid<detail::grid_test_tag_, 2, 3>::consumer_tags,
                             std::tuple<Consumer<detail::grid_test_tag_, 0>, Consumer<detail::grid_test_tag_, 1>,
                                        Consumer<detail::grid_test_tag_, 2>>>);

inline void runtime_smoke_test_grid() {
    using Tag = detail::grid_test_tag_;

    auto whole = mint_permission_root<Tag>();
    auto grid = mint_grid_permissions<Tag, 4, 3>(std::move(whole));

    static_assert(std::is_same_v<decltype(grid), GridPermissions<Tag, 4, 3>>);
    static_assert(std::is_same_v<decltype(grid.producers),
                                 std::tuple<Permission<Producer<Tag, 0>>, Permission<Producer<Tag, 1>>,
                                            Permission<Producer<Tag, 2>>, Permission<Producer<Tag, 3>>>>);
    static_assert(
        std::is_same_v<decltype(grid.consumers), std::tuple<Permission<Consumer<Tag, 0>>, Permission<Consumer<Tag, 1>>,
                                                            Permission<Consumer<Tag, 2>>>>);

    (void)grid;
}

}  // namespace crucible::safety
