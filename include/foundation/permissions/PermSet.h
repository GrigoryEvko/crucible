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

// Every question this header asks of a PermSet is a question about its
// tag list, and the template arguments are that list.  The old header
// asked each question with a primary template plus a partial
// specialization that destructured the pack, and so spelled the same
// destructuring once per question.  The helpers below destructure by
// reflection instead, and each public trait is one call.
//
// A reflection is compared after dealias, so a tag written against
// `using Alias = Concrete;` is the tag Concrete.  An empty PermSet
// yields an empty argument range, which every loop below reads as the
// empty set rather than by indexing.

namespace detail {

// True when set, the reflection of a PermSet, names tag.
[[nodiscard]] consteval bool perm_set_names_(std::meta::info set, std::meta::info tag) noexcept {
    for (std::meta::info member : std::meta::template_arguments_of(set)) {
        if (std::meta::dealias(member) == std::meta::dealias(tag)) return true;
    }
    return false;
}

// Every tag of sub is a tag of super.  An empty sub is a subset of
// every set, which is what the empty conjunction answered.
[[nodiscard]] consteval bool perm_set_subset_(std::meta::info sub, std::meta::info super) noexcept {
    for (std::meta::info tag : std::meta::template_arguments_of(sub)) {
        if (!perm_set_names_(super, tag)) return false;
    }
    return true;
}

// No tag of lhs is a tag of rhs.  An empty operand makes the two
// disjoint, which is what the empty conjunction answered.
[[nodiscard]] consteval bool perm_set_disjoint_(std::meta::info lhs, std::meta::info rhs) noexcept {
    for (std::meta::info tag : std::meta::template_arguments_of(lhs)) {
        if (perm_set_names_(rhs, tag)) return false;
    }
    return true;
}

// The reflection of set with tag at the head, or of set itself when set
// already names tag.  The head position is where the old insert branch
// put a new tag.  The already-present arm returns the operand rather
// than a rebuilt one, so it cannot differ from it in any way.
[[nodiscard]] consteval std::meta::info perm_set_insert_(std::meta::info set, std::meta::info tag) {
    if (perm_set_names_(set, tag)) return set;
    std::vector<std::meta::info> tags{tag};
    for (std::meta::info member : std::meta::template_arguments_of(set)) {
        tags.push_back(member);
    }
    return std::meta::substitute(^^PermSet, tags);
}

// The reflection of PermSet<kept...>, where kept is every tag of PS that
// keep admits, in the order PS lists them.  remove and difference are
// this one filter under two predicates.
template <typename PS, typename Keep>
[[nodiscard]] consteval std::meta::info perm_set_filter_(Keep keep) {
    std::vector<std::meta::info> kept;
    for (std::meta::info tag : std::meta::template_arguments_of(^^PS)) {
        if (keep(tag)) kept.push_back(tag);
    }
    return std::meta::substitute(^^PermSet, kept);
}

// The tags of lhs followed by the tags of rhs.  The caller states the
// disjointness this relies on.
[[nodiscard]] consteval std::meta::info perm_set_concat_(std::meta::info lhs, std::meta::info rhs) {
    std::vector<std::meta::info> tags;
    for (std::meta::info tag : std::meta::template_arguments_of(lhs)) {
        tags.push_back(tag);
    }
    for (std::meta::info tag : std::meta::template_arguments_of(rhs)) {
        tags.push_back(tag);
    }
    return std::meta::substitute(^^PermSet, tags);
}

}  // namespace detail

template <typename PS, typename Q>
inline constexpr bool perm_set_contains_v = detail::perm_set_names_(^^PS, ^^Q);

template <typename PS, typename Q>
struct perm_set_insert {
    using type = [:detail::perm_set_insert_(^^PS, ^^Q):];
};

template <typename PS, typename Q>
using perm_set_insert_t = typename perm_set_insert<PS, Q>::type;

template <typename PS, typename Q>
struct perm_set_remove {
    using type = [:detail::perm_set_filter_<PS>(
                       [](std::meta::info tag) { return std::meta::dealias(tag) != std::meta::dealias(^^Q); }):];
};

template <typename PS, typename Q>
using perm_set_remove_t = typename perm_set_remove<PS, Q>::type;

template <typename PS1, typename PS2>
inline constexpr bool perm_set_subset_v = detail::perm_set_subset_(^^PS1, ^^PS2);

template <typename PS1, typename PS2>
inline constexpr bool perm_set_disjoint_v = detail::perm_set_disjoint_(^^PS1, ^^PS2);

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
//
// The assertion sits in the class the call site names, so the compiler
// reports it with that call site in the backtrace.  Moving it into a
// helper the class calls would report the helper instead.

template <typename PS1, typename PS2>
struct perm_set_union {
    static_assert(detail::perm_set_disjoint_(^^PS1, ^^PS2),
                  "foundation::permissions [PermissionImbalance]: "
                  "perm_set_union_t requires disjoint operands — a permission "
                  "tag appears in both PermSets.  A CSL permission cannot be "
                  "held by two participants simultaneously.  Verify the call "
                  "site does not double-insert a tag, and check that "
                  "mint_permission_split's children remain disjoint along the "
                  "session protocol.");
    using type = [:detail::perm_set_concat_(^^PS1, ^^PS2):];
};

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

// The reflection compares after dealias, so a tag reached through an
// alias is the tag itself.  The is_same_v fold these traits replace read
// through an alias as well, and these pins hold the new spelling to that
// behavior.
using A_alias = A_tag;

static_assert(perm_set_contains_v<PermSet<A_tag>, A_alias>);
static_assert(perm_set_contains_v<PermSet<A_alias>, A_tag>);
static_assert(perm_set_subset_v<PermSet<A_alias>, PermSet<A_tag>>);
static_assert(!perm_set_disjoint_v<PermSet<A_alias>, PermSet<A_tag>>);
static_assert(std::is_same_v<perm_set_insert_t<PermSet<A_alias>, A_tag>, PermSet<A_tag>>);
static_assert(std::is_same_v<perm_set_remove_t<PermSet<A_alias, B_tag>, A_tag>, PermSet<B_tag>>);
static_assert(std::is_same_v<perm_set_difference_t<PermSet<A_alias, B_tag>, PermSet<A_tag>>, PermSet<B_tag>>);

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
