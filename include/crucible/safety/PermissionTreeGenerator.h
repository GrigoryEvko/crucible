#pragma once

#include <crucible/permissions/_Permission.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename Parent, std::size_t I>
struct Slice {
    using parent_type = Parent;
    static constexpr std::size_t index = I;
};

// Index-pack deduction covers every arity in one specialization, so a caller
// splitting a parent into N shards declares nothing per N.

template <typename Parent, std::size_t... Is>
struct splits_into_pack<Parent, Slice<Parent, Is>...> : std::true_type {};

// Specialize this witness in lockstep with the specialization above.
template <typename Parent, std::size_t... Is>
struct splits_into_pack_authoring_witness<Parent, Slice<Parent, Is>...> : std::true_type {};

namespace detail {

template <typename Parent, std::size_t... Is>
constexpr auto auto_split_tuple_(std::index_sequence<Is...>) noexcept -> std::tuple<Slice<Parent, Is>...>;

template <typename Parent, std::size_t... Is>
constexpr auto auto_split_perms_(std::index_sequence<Is...>) noexcept -> std::tuple<Permission<Slice<Parent, Is>>...>;

}  // namespace detail

template <typename Parent, std::size_t N>
struct auto_split_n {
    static_assert(N > 0, "auto_split_n<Parent, N>: N must be greater than zero — "
                         "splitting a permission into zero shards has no operational meaning");

    using type = decltype(detail::auto_split_tuple_<Parent>(std::make_index_sequence<N>{}));

    using permissions_type = decltype(detail::auto_split_perms_<Parent>(std::make_index_sequence<N>{}));
};

template <typename Parent, std::size_t N>
using auto_split_n_t = typename auto_split_n<Parent, N>::type;

template <typename Parent, std::size_t N>
using auto_split_n_permissions_t = typename auto_split_n<Parent, N>::permissions_type;

// The specialization above is universal, so this holds for every N above zero.
// It is published as the capability check so a caller asks the trait rather
// than assuming, which leaves room to refuse some N later.

namespace detail {

template <typename Parent, std::size_t... Is>
constexpr bool can_split_n_impl_(std::index_sequence<Is...>) noexcept {
    return splits_into_pack_v<Parent, Slice<Parent, Is>...>;
}

}  // namespace detail

template <typename Parent, std::size_t N>
inline constexpr bool can_split_n_v = (N > 0) && detail::can_split_n_impl_<Parent>(std::make_index_sequence<N>{});

namespace detail {
struct ptg_test_tag_ {};
}  // namespace detail

static_assert(sizeof(Slice<detail::ptg_test_tag_, 0>) == 1,
              "Slice<Parent, I>: must be a 1-byte empty class (no payload)");
static_assert(std::is_trivially_destructible_v<Slice<detail::ptg_test_tag_, 0>>);
static_assert(std::is_empty_v<Slice<detail::ptg_test_tag_, 0>>);

static_assert(!std::is_same_v<Slice<detail::ptg_test_tag_, 0>, Slice<detail::ptg_test_tag_, 1>>);

static_assert(std::is_same_v<Slice<detail::ptg_test_tag_, 5>::parent_type, detail::ptg_test_tag_>);
static_assert(Slice<detail::ptg_test_tag_, 5>::index == 5);

static_assert(splits_into_pack_v<detail::ptg_test_tag_, Slice<detail::ptg_test_tag_, 0>>);
static_assert(
    splits_into_pack_v<detail::ptg_test_tag_, Slice<detail::ptg_test_tag_, 0>, Slice<detail::ptg_test_tag_, 1>,
                       Slice<detail::ptg_test_tag_, 2>, Slice<detail::ptg_test_tag_, 3>>);

static_assert(can_split_n_v<detail::ptg_test_tag_, 1>);
static_assert(can_split_n_v<detail::ptg_test_tag_, 4>);
static_assert(can_split_n_v<detail::ptg_test_tag_, 64>);

static_assert(std::is_same_v<auto_split_n_t<detail::ptg_test_tag_, 3>,
                             std::tuple<Slice<detail::ptg_test_tag_, 0>, Slice<detail::ptg_test_tag_, 1>,
                                        Slice<detail::ptg_test_tag_, 2>>>);

static_assert(std::is_same_v<
              auto_split_n_permissions_t<detail::ptg_test_tag_, 2>,
              std::tuple<Permission<Slice<detail::ptg_test_tag_, 0>>, Permission<Slice<detail::ptg_test_tag_, 1>>>>);

inline void runtime_smoke_test() {
    using Tag = detail::ptg_test_tag_;
    auto parent = mint_permission_root<Tag>();

    auto children =
        mint_permission_split_n<Slice<Tag, 0>, Slice<Tag, 1>, Slice<Tag, 2>, Slice<Tag, 3>>(std::move(parent));

    static_assert(std::is_same_v<decltype(children), auto_split_n_permissions_t<Tag, 4>>);

    (void)children;
}

}  // namespace crucible::safety
