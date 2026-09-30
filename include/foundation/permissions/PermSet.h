#pragma once

// PermSet<Tags...> is the set of permission tags that a session handle
// holds at one position of its protocol.  The functions and aliases below
// ask set questions of it: contains, insert, remove, subset, union,
// difference.

#include <foundation/Platform.h>
#include <foundation/diag/RowHash.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <vector>

namespace foundation::permissions {

// The unique-tag test is the one the split manifests ask of a child pack:
// DistinctTags in Permission.h.
template <typename... Tags>
struct PermSet {
    static_assert(DistinctTags<Tags...>, "foundation::permissions [PermissionImbalance]: "
                                         "PermSet<Tags...> requires unique Tags.  A duplicate permission "
                                         "tag means the same CSL authority was inserted twice, usually by "
                                         "passing the same Permission<Tag> token through a mint boundary "
                                         "more than once.");

    static constexpr std::size_t size = sizeof...(Tags);
};

using EmptyPermSet = PermSet<>;

// Every question this header asks of a PermSet is a question about its
// tag list, and the template arguments are that list.  The helpers below
// destructure the list by reflection.  Each public question is a function
// at namespace scope that is not a template, and each public operation is
// an alias over one call, so no translation unit can specialize either to
// give a handle a permission it does not hold.
//
// A reflection is compared after dealias, so a tag written against
// `using Alias = Concrete;` is the tag Concrete.  An empty PermSet
// yields an empty argument range, which every loop below reads as the
// empty set rather than by indexing.

namespace detail {

// The tags of set, the reflection of a PermSet, read through the aliases
// of the set.
[[nodiscard]] consteval std::vector<std::meta::info> perm_set_tags_(std::meta::info set) {
    return std::meta::template_arguments_of(std::meta::dealias(set));
}

// True when set, the reflection of a PermSet, names tag.
[[nodiscard]] consteval bool perm_set_names_(std::meta::info set, std::meta::info tag) noexcept {
    for (std::meta::info member : perm_set_tags_(set)) {
        if (std::meta::dealias(member) == std::meta::dealias(tag)) return true;
    }
    return false;
}

// Every tag of sub is a tag of super.  An empty sub is a subset of
// every set.
[[nodiscard]] consteval bool perm_set_subset_(std::meta::info sub, std::meta::info super) noexcept {
    for (std::meta::info tag : perm_set_tags_(sub)) {
        if (!perm_set_names_(super, tag)) return false;
    }
    return true;
}

// No tag of lhs is a tag of rhs.  An empty operand makes the two
// disjoint.
[[nodiscard]] consteval bool perm_set_disjoint_(std::meta::info lhs, std::meta::info rhs) noexcept {
    for (std::meta::info tag : perm_set_tags_(lhs)) {
        if (perm_set_names_(rhs, tag)) return false;
    }
    return true;
}

// The reflection of set with tag at the head, or of set itself when set
// already names tag.  The already-present arm returns the operand rather
// than a rebuilt one, so it cannot differ from it in any way.
[[nodiscard]] consteval std::meta::info perm_set_insert_(std::meta::info set, std::meta::info tag) {
    if (perm_set_names_(set, tag)) return set;
    std::vector<std::meta::info> tags{tag};
    for (std::meta::info member : perm_set_tags_(set)) {
        tags.push_back(member);
    }
    return std::meta::substitute(^^PermSet, tags);
}

// The reflection of PermSet<kept...>, where kept is every tag of set that
// other does not name, in the order set lists them.  remove and difference
// are this one filter, with other a set of one tag or a set of many.
[[nodiscard]] consteval std::meta::info perm_set_without_(std::meta::info set, std::meta::info other) {
    std::vector<std::meta::info> kept;
    for (std::meta::info tag : perm_set_tags_(set)) {
        if (!perm_set_names_(other, tag)) kept.push_back(tag);
    }
    return std::meta::substitute(^^PermSet, kept);
}

// The reflection of set with tag removed, or of set itself when set does
// not name tag.
[[nodiscard]] consteval std::meta::info perm_set_remove_(std::meta::info set, std::meta::info tag) {
    return perm_set_without_(set, std::meta::substitute(^^PermSet, {tag}));
}

// The tags of lhs followed by the tags of rhs.  The two sets must be
// disjoint, and a union of two sets that share a tag stops the build.
//
// The message spells its classification prefix as a literal rather than
// pulling it from the session diagnostic catalog, which would cost this
// header that whole dependency.  The literal has to match the catalog's
// tag.
[[nodiscard]] consteval std::meta::info perm_set_union_(std::meta::info lhs, std::meta::info rhs) {
    if (!perm_set_disjoint_(lhs, rhs)) {
        throw std::meta::exception(u8"foundation::permissions [PermissionImbalance]: perm_set_union_t requires "
                                   u8"disjoint operands.  A permission tag appears in both PermSets, and a CSL "
                                   u8"permission cannot be held by two participants at the same time.  Make sure "
                                   u8"that the call site does not insert a tag twice, and that the children of "
                                   u8"mint_permission_split stay disjoint along the session protocol.",
                                   lhs);
    }
    std::vector<std::meta::info> tags;
    for (std::meta::info tag : perm_set_tags_(lhs)) {
        tags.push_back(tag);
    }
    for (std::meta::info tag : perm_set_tags_(rhs)) {
        tags.push_back(tag);
    }
    return std::meta::substitute(^^PermSet, tags);
}

}  // namespace detail

// True when the set names the tag.
[[nodiscard]] consteval bool perm_set_contains(std::meta::info set, std::meta::info tag) {
    return detail::perm_set_names_(set, tag);
}

// True when each tag of sub is a tag of super.
[[nodiscard]] consteval bool perm_set_subset(std::meta::info sub, std::meta::info super) {
    return detail::perm_set_subset_(sub, super);
}

// True when no tag of lhs is a tag of rhs.
[[nodiscard]] consteval bool perm_set_disjoint(std::meta::info lhs, std::meta::info rhs) {
    return detail::perm_set_disjoint_(lhs, rhs);
}

// Bidirectional containment, so the comparison is insensitive to the
// order of the packs.  Sorting both packs into a canonical form would
// reduce this to one type comparison, but a permission set holds a
// handful of tags, so the quadratic form costs nothing and needs no sort
// machinery.
[[nodiscard]] consteval bool perm_set_equal(std::meta::info lhs, std::meta::info rhs) {
    return detail::perm_set_tags_(lhs).size() == detail::perm_set_tags_(rhs).size()
        && detail::perm_set_subset_(lhs, rhs) && detail::perm_set_subset_(rhs, lhs);
}

template <typename PS, typename Q>
using perm_set_insert_t = [:detail::perm_set_insert_(^^PS, ^^Q):];

template <typename PS, typename Q>
using perm_set_remove_t = [:detail::perm_set_remove_(^^PS, ^^Q):];

template <typename PS1, typename PS2>
using perm_set_union_t = [:detail::perm_set_union_(^^PS1, ^^PS2):];

template <typename PS1, typename PS2>
using perm_set_difference_t = [:detail::perm_set_without_(^^PS1, ^^PS2):];

// Identity suffices for equality, which compares by containment rather
// than by canonical form.  A consumer that wants a hashable canonical
// form needs a real sort here.  The row hash at the foot of this header
// sorts for itself, so it does not depend on this.
template <typename PS>
using perm_set_canonicalize_t = PS;

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

static_assert(!perm_set_contains(^^EmptyPermSet, ^^A_tag));
static_assert(perm_set_contains(^^PermSet<A_tag>, ^^A_tag));
static_assert(!perm_set_contains(^^PermSet<A_tag>, ^^B_tag));
static_assert(perm_set_contains(^^PermSet<A_tag, B_tag, C_tag>, ^^B_tag));
static_assert(!perm_set_contains(^^PermSet<A_tag, B_tag, C_tag>, ^^D_tag));

// The reflection compares after dealias, so a tag reached through an
// alias is the tag itself, and these pins hold the functions to that.
using A_alias = A_tag;

static_assert(perm_set_contains(^^PermSet<A_tag>, ^^A_alias));
static_assert(perm_set_contains(^^PermSet<A_alias>, ^^A_tag));
static_assert(perm_set_subset(^^PermSet<A_alias>, ^^PermSet<A_tag>));
static_assert(!perm_set_disjoint(^^PermSet<A_alias>, ^^PermSet<A_tag>));
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

static_assert(perm_set_subset(^^EmptyPermSet, ^^EmptyPermSet));
static_assert(perm_set_subset(^^EmptyPermSet, ^^PermSet<A_tag>));
static_assert(perm_set_subset(^^PermSet<A_tag>, ^^PermSet<A_tag>));
static_assert(perm_set_subset(^^PermSet<A_tag>, ^^PermSet<A_tag, B_tag>));
static_assert(!perm_set_subset(^^PermSet<A_tag, C_tag>, ^^PermSet<A_tag, B_tag>));
static_assert(!perm_set_subset(^^PermSet<A_tag>, ^^EmptyPermSet));

static_assert(perm_set_disjoint(^^EmptyPermSet, ^^EmptyPermSet));
static_assert(perm_set_disjoint(^^EmptyPermSet, ^^PermSet<A_tag>));
static_assert(perm_set_disjoint(^^PermSet<A_tag>, ^^EmptyPermSet));
static_assert(perm_set_disjoint(^^PermSet<A_tag>, ^^PermSet<B_tag, C_tag>));
static_assert(!perm_set_disjoint(^^PermSet<A_tag>, ^^PermSet<A_tag>));
static_assert(!perm_set_disjoint(^^PermSet<A_tag, B_tag>, ^^PermSet<C_tag, B_tag>));

static_assert(perm_set_equal(^^EmptyPermSet, ^^EmptyPermSet));
static_assert(perm_set_equal(^^PermSet<A_tag>, ^^PermSet<A_tag>));
static_assert(perm_set_equal(^^PermSet<A_tag, B_tag>, ^^PermSet<B_tag, A_tag>));
static_assert(perm_set_equal(^^PermSet<A_tag, B_tag, C_tag>, ^^PermSet<C_tag, A_tag, B_tag>));
static_assert(!perm_set_equal(^^PermSet<A_tag>, ^^PermSet<B_tag>));
static_assert(!perm_set_equal(^^PermSet<A_tag>, ^^PermSet<A_tag, B_tag>));
static_assert(!perm_set_equal(^^PermSet<A_tag, B_tag>, ^^PermSet<A_tag, C_tag>));

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

}  // namespace foundation::permissions::detail::permset_smoke

namespace foundation::permissions::row_discipline {
struct perm_set;
}  // namespace foundation::permissions::row_discipline

// A permission set is a grade, as an effect row is: it names the
// authorities a session position holds.  It hashes as a set for the same
// reason a row does, so two spellings of one set share a slot.  The tags
// are the elements here, not identities of an instance, so they fold,
// sorted by canonical id and seeded with the count.
namespace foundation::diag {

template <typename... Tags>
struct row_hash_contribution<::foundation::permissions::PermSet<Tags...>> {
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        constexpr std::size_t N = sizeof...(Tags);
        std::array<std::uint64_t, N> const ids{lattice_canonical_id_v<Tags>...};
        auto const sorted = detail::sorted_uints(ids);
        std::uint64_t const elements =
            detail::fmix64_fold_unique_sorted(sorted, detail::cardinality_seed(detail::unique_count_sorted(sorted)));
        return detail::combine_ids(
            discipline_row_hash_v<::foundation::permissions::row_discipline::perm_set, row_payloads<>>, elements);
    }();
};

}  // namespace foundation::diag
