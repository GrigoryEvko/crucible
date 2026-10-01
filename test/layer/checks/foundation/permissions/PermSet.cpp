// The compile-time checks of foundation/permissions/PermSet.h.

#include <foundation/permissions/PermSet.h>

namespace foundation::permissions::detail::permset_smoke {

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
