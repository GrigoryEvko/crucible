#pragma once

// Old spelling: include/crucible/permissions/PermSet.h, namespace
// crucible::safety::proto.

#include <foundation/Platform.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>

namespace foundation::permissions {

namespace detail {

template <typename... Tags>
struct perm_tags_unique_impl : std::true_type {};

template <typename Head, typename... Tail>
struct perm_tags_unique_impl<Head, Tail...>
    : std::bool_constant<((!std::is_same_v<Head, Tail>) && ...) && perm_tags_unique_impl<Tail...>::value> {};

template <typename... Tags>
inline constexpr bool perm_tags_unique_v = perm_tags_unique_impl<Tags...>::value;

}  // namespace detail

template <typename... Tags>
struct PermSet {
    static_assert(detail::perm_tags_unique_v<Tags...>,
                  "foundation::permissions [PermissionImbalance]: "
                  "PermSet<Tags...> requires unique Tags.  A duplicate permission "
                  "tag means the same CSL authority was inserted twice, usually by "
                  "passing the same Permission<Tag> token through a mint boundary "
                  "more than once.");

    static constexpr std::size_t size = sizeof...(Tags);
};

using EmptyPermSet = PermSet<>;

namespace detail {

template <typename PS, typename Q>
struct perm_set_contains_impl;

template <typename... Ts, typename Q>
struct perm_set_contains_impl<PermSet<Ts...>, Q> : std::bool_constant<(std::is_same_v<Ts, Q> || ...)> {};

}  // namespace detail

template <typename PS, typename Q>
inline constexpr bool perm_set_contains_v = detail::perm_set_contains_impl<PS, Q>::value;

namespace detail {

template <typename PS, typename Q, bool Already>
struct perm_set_insert_branch;

template <typename... Ts, typename Q>
struct perm_set_insert_branch<PermSet<Ts...>, Q, /*Already=*/true> {
    using type = PermSet<Ts...>;
};

template <typename... Ts, typename Q>
struct perm_set_insert_branch<PermSet<Ts...>, Q, /*Already=*/false> {
    using type = PermSet<Q, Ts...>;
};

}  // namespace detail

template <typename PS, typename Q>
struct perm_set_insert : detail::perm_set_insert_branch<PS, Q, perm_set_contains_v<PS, Q>> {};

template <typename PS, typename Q>
using perm_set_insert_t = typename perm_set_insert<PS, Q>::type;

namespace detail {

template <typename PS, typename Q>
struct perm_set_remove_impl;

template <typename Q>
struct perm_set_remove_impl<PermSet<>, Q> {
    using type = PermSet<>;
};

template <typename Head, typename... Tail, typename Q>
struct perm_set_remove_impl<PermSet<Head, Tail...>, Q> {
    using rec_type = typename perm_set_remove_impl<PermSet<Tail...>, Q>::type;

    template <typename S>
    struct prepend_head;

    template <typename... Xs>
    struct prepend_head<PermSet<Xs...>> {
        using type = PermSet<Head, Xs...>;
    };

    using type = std::conditional_t<std::is_same_v<Head, Q>, rec_type, typename prepend_head<rec_type>::type>;
};

}  // namespace detail

template <typename PS, typename Q>
struct perm_set_remove : detail::perm_set_remove_impl<PS, Q> {};

template <typename PS, typename Q>
using perm_set_remove_t = typename perm_set_remove<PS, Q>::type;

namespace detail {

template <typename PS1, typename PS2>
struct perm_set_subset_impl;

template <typename... T1s, typename PS2>
struct perm_set_subset_impl<PermSet<T1s...>, PS2> : std::bool_constant<(perm_set_contains_v<PS2, T1s> && ...)> {};

}  // namespace detail

template <typename PS1, typename PS2>
inline constexpr bool perm_set_subset_v = detail::perm_set_subset_impl<PS1, PS2>::value;

namespace detail {

template <typename PS1, typename PS2>
struct perm_set_disjoint_impl;

template <typename... T1s, typename PS2>
struct perm_set_disjoint_impl<PermSet<T1s...>, PS2> : std::bool_constant<((!perm_set_contains_v<PS2, T1s>) && ...)> {};

}  // namespace detail

template <typename PS1, typename PS2>
inline constexpr bool perm_set_disjoint_v = detail::perm_set_disjoint_impl<PS1, PS2>::value;

// Bidirectional containment, so the comparison is insensitive to the
// order of the packs.  Sorting both packs into a canonical form would
// reduce this to one type comparison, but a permission set holds a
// handful of tags, so the quadratic form costs nothing and needs no sort
// machinery.

template <typename PS1, typename PS2>
inline constexpr bool perm_set_equal_v =
    PS1::size == PS2::size && perm_set_subset_v<PS1, PS2> && perm_set_subset_v<PS2, PS1>;

// The diagnostic below spells its classification prefix as a literal
// rather than pulling it from the session diagnostic catalog, which
// would cost this header that whole dependency.  The literal has to
// match the catalog's tag.

namespace detail {

template <typename PS1, typename PS2>
struct perm_set_union_impl;

template <typename... T1s, typename... T2s>
struct perm_set_union_impl<PermSet<T1s...>, PermSet<T2s...>> {
    static_assert(((!perm_set_contains_v<PermSet<T1s...>, T2s>) && ...),
                  "foundation::permissions [PermissionImbalance]: "
                  "perm_set_union_t requires disjoint operands — a permission "
                  "tag appears in both PermSets.  A CSL permission cannot be "
                  "held by two participants simultaneously.  Verify the call "
                  "site does not double-insert a tag, and check that "
                  "mint_permission_split's children remain disjoint along the "
                  "session protocol.");
    using type = PermSet<T1s..., T2s...>;
};

}  // namespace detail

template <typename PS1, typename PS2>
struct perm_set_union : detail::perm_set_union_impl<PS1, PS2> {};

template <typename PS1, typename PS2>
using perm_set_union_t = typename perm_set_union<PS1, PS2>::type;

namespace detail {

template <typename PS1, typename PS2>
struct perm_set_difference_impl;

template <typename PS2>
struct perm_set_difference_impl<PermSet<>, PS2> {
    using type = PermSet<>;
};

template <typename Head, typename... Tail, typename PS2>
struct perm_set_difference_impl<PermSet<Head, Tail...>, PS2> {
    using rec_type = typename perm_set_difference_impl<PermSet<Tail...>, PS2>::type;

    template <typename S>
    struct prepend_head;

    template <typename... Xs>
    struct prepend_head<PermSet<Xs...>> {
        using type = PermSet<Head, Xs...>;
    };

    using type = std::conditional_t<perm_set_contains_v<PS2, Head>, rec_type, typename prepend_head<rec_type>::type>;
};

}  // namespace detail

template <typename PS1, typename PS2>
struct perm_set_difference : detail::perm_set_difference_impl<PS1, PS2> {};

template <typename PS1, typename PS2>
using perm_set_difference_t = typename perm_set_difference<PS1, PS2>::type;

// Identity suffices for equality, which compares by containment rather
// than by canonical form.  A consumer that wants a hashable canonical
// form needs a real sort here.

template <typename PS>
struct perm_set_canonicalize {
    using type = PS;
};

template <typename PS>
using perm_set_canonicalize_t = typename perm_set_canonicalize<PS>::type;

// The string a reflected display name produces depends on the including
// translation unit's context, so an assertion over it matches a suffix
// rather than the whole name.

template <typename PS>
[[nodiscard]] consteval std::string_view perm_set_name() noexcept {
    return std::meta::display_string_of(^^PS);
}

}  // namespace foundation::permissions

namespace foundation::permissions::detail::permset_smoke {

struct A_tag {};
struct B_tag {};
struct C_tag {};
struct D_tag {};

static_assert(EmptyPermSet::size == 0);
static_assert(PermSet<A_tag>::size == 1);
static_assert(PermSet<A_tag, B_tag, C_tag>::size == 3);

static_assert(!perm_set_contains_v<EmptyPermSet, A_tag>);
static_assert(perm_set_contains_v<PermSet<A_tag>, A_tag>);
static_assert(!perm_set_contains_v<PermSet<A_tag>, B_tag>);
static_assert(perm_set_contains_v<PermSet<A_tag, B_tag, C_tag>, B_tag>);
static_assert(!perm_set_contains_v<PermSet<A_tag, B_tag, C_tag>, D_tag>);

static_assert(std::is_same_v<perm_set_insert_t<EmptyPermSet, A_tag>, PermSet<A_tag>>);
static_assert(std::is_same_v<perm_set_insert_t<PermSet<B_tag>, A_tag>, PermSet<A_tag, B_tag>>);
static_assert(std::is_same_v<perm_set_insert_t<PermSet<A_tag>, A_tag>, PermSet<A_tag>>);
static_assert(std::is_same_v<perm_set_insert_t<PermSet<A_tag, B_tag>, C_tag>, PermSet<C_tag, A_tag, B_tag>>);

static_assert(std::is_same_v<perm_set_remove_t<EmptyPermSet, A_tag>, PermSet<>>);
static_assert(std::is_same_v<perm_set_remove_t<PermSet<A_tag>, A_tag>, PermSet<>>);
static_assert(std::is_same_v<perm_set_remove_t<PermSet<A_tag>, B_tag>, PermSet<A_tag>>);
static_assert(std::is_same_v<perm_set_remove_t<PermSet<A_tag, B_tag>, A_tag>, PermSet<B_tag>>);
static_assert(std::is_same_v<perm_set_remove_t<PermSet<A_tag, B_tag, C_tag>, B_tag>, PermSet<A_tag, C_tag>>);

static_assert(perm_set_subset_v<EmptyPermSet, EmptyPermSet>);
static_assert(perm_set_subset_v<EmptyPermSet, PermSet<A_tag>>);
static_assert(perm_set_subset_v<PermSet<A_tag>, PermSet<A_tag>>);
static_assert(perm_set_subset_v<PermSet<A_tag>, PermSet<A_tag, B_tag>>);
static_assert(!perm_set_subset_v<PermSet<A_tag, C_tag>, PermSet<A_tag, B_tag>>);
static_assert(!perm_set_subset_v<PermSet<A_tag>, EmptyPermSet>);

static_assert(perm_set_disjoint_v<EmptyPermSet, EmptyPermSet>);
static_assert(perm_set_disjoint_v<EmptyPermSet, PermSet<A_tag>>);
static_assert(perm_set_disjoint_v<PermSet<A_tag>, EmptyPermSet>);
static_assert(perm_set_disjoint_v<PermSet<A_tag>, PermSet<B_tag, C_tag>>);
static_assert(!perm_set_disjoint_v<PermSet<A_tag>, PermSet<A_tag>>);
static_assert(!perm_set_disjoint_v<PermSet<A_tag, B_tag>, PermSet<C_tag, B_tag>>);

static_assert(perm_set_equal_v<EmptyPermSet, EmptyPermSet>);
static_assert(perm_set_equal_v<PermSet<A_tag>, PermSet<A_tag>>);
static_assert(perm_set_equal_v<PermSet<A_tag, B_tag>, PermSet<B_tag, A_tag>>);
static_assert(perm_set_equal_v<PermSet<A_tag, B_tag, C_tag>, PermSet<C_tag, A_tag, B_tag>>);
static_assert(!perm_set_equal_v<PermSet<A_tag>, PermSet<B_tag>>);
static_assert(!perm_set_equal_v<PermSet<A_tag>, PermSet<A_tag, B_tag>>);
static_assert(!perm_set_equal_v<PermSet<A_tag, B_tag>, PermSet<A_tag, C_tag>>);

static_assert(std::is_same_v<perm_set_union_t<EmptyPermSet, EmptyPermSet>, PermSet<>>);
static_assert(std::is_same_v<perm_set_union_t<EmptyPermSet, PermSet<A_tag>>, PermSet<A_tag>>);
static_assert(std::is_same_v<perm_set_union_t<PermSet<A_tag>, PermSet<B_tag>>, PermSet<A_tag, B_tag>>);
static_assert(std::is_same_v<perm_set_union_t<PermSet<A_tag, B_tag>, PermSet<C_tag>>, PermSet<A_tag, B_tag, C_tag>>);

static_assert(std::is_same_v<perm_set_difference_t<EmptyPermSet, PermSet<A_tag>>, PermSet<>>);
static_assert(std::is_same_v<perm_set_difference_t<PermSet<A_tag, B_tag>, EmptyPermSet>, PermSet<A_tag, B_tag>>);
static_assert(std::is_same_v<perm_set_difference_t<PermSet<A_tag>, PermSet<A_tag>>, PermSet<>>);
static_assert(
    std::is_same_v<perm_set_difference_t<PermSet<A_tag, B_tag, C_tag>, PermSet<B_tag>>, PermSet<A_tag, C_tag>>);
static_assert(
    std::is_same_v<perm_set_difference_t<PermSet<A_tag, B_tag, C_tag>, PermSet<A_tag, C_tag>>, PermSet<B_tag>>);

static_assert(std::is_same_v<perm_set_canonicalize_t<PermSet<A_tag, B_tag, C_tag>>, PermSet<A_tag, B_tag, C_tag>>);

static_assert(sizeof(EmptyPermSet) == 1);
static_assert(sizeof(PermSet<A_tag>) == 1);
static_assert(sizeof(PermSet<A_tag, B_tag, C_tag, D_tag>) == 1);
static_assert(std::is_trivially_destructible_v<PermSet<A_tag>>);
static_assert(std::is_empty_v<PermSet<A_tag, B_tag, C_tag>>);

inline void runtime_smoke_test() noexcept {
    constexpr auto empty_size = EmptyPermSet::size;
    constexpr auto three_size = PermSet<A_tag, B_tag, C_tag>::size;
    static_assert(empty_size == 0);
    static_assert(three_size == 3);

    constexpr auto name = perm_set_name<PermSet<A_tag, B_tag>>();
    static_assert(!name.empty());

    static_assert(perm_set_equal_v<perm_set_canonicalize_t<PermSet<A_tag, B_tag>>,
                                   perm_set_canonicalize_t<PermSet<B_tag, A_tag>>>);
}

}  // namespace foundation::permissions::detail::permset_smoke
