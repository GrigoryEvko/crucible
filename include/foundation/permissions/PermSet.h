#pragma once

// Old spelling: include/crucible/permissions/PermSet.h, namespace
// crucible::safety::proto.

#include <foundation/Platform.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>
#include <vector>

namespace foundation::permissions {

namespace detail {

// The same question the split manifests ask of a child pack, so the
// same answer: Permission.h's pairwise-distinct fold.
template <typename... Tags>
inline constexpr bool perm_tags_unique_v = all_distinct_tags_v<Tags...>;

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

// remove and difference are one filter: the tags of a set that pass a
// predicate, rebuilt as a PermSet.  The old header wrote each as its own
// head-and-tail recursion with a prepend helper; reflection walks the
// template arguments as a list and substitutes the survivors back in.

namespace detail {

// True when set, the reflection of a PermSet, names tag.  The two are
// read through their aliases, so a tag written against
// `using Alias = Concrete;` is the tag Concrete.
[[nodiscard]] consteval bool perm_set_names_(std::meta::info set, std::meta::info tag) noexcept {
    for (std::meta::info member : std::meta::template_arguments_of(set)) {
        if (std::meta::dealias(member) == std::meta::dealias(tag)) return true;
    }
    return false;
}

// The reflection of PermSet<kept...>, where kept is every tag of PS that
// keep admits, in the order PS lists them.
template <typename PS, typename Keep>
[[nodiscard]] consteval std::meta::info perm_set_filter_(Keep keep) {
    std::vector<std::meta::info> kept;
    for (std::meta::info tag : std::meta::template_arguments_of(^^PS)) {
        if (keep(tag)) kept.push_back(tag);
    }
    return std::meta::substitute(^^PermSet, kept);
}

}  // namespace detail

template <typename PS, typename Q>
struct perm_set_remove {
    using type = [:detail::perm_set_filter_<PS>(
                       [](std::meta::info tag) { return std::meta::dealias(tag) != std::meta::dealias(^^Q); }):];
};

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

template <typename PS1, typename PS2>
struct perm_set_difference {
    using type = [:detail::perm_set_filter_<PS1>(
                       [](std::meta::info tag) { return !detail::perm_set_names_(^^PS2, tag); }):];
};

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
