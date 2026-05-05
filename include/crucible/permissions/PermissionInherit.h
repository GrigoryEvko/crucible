#pragma once

// ── crucible::permissions::permission_inherit — crash-stop CSL recovery ──
//
// GAPS-044 foundation for the CrashWatchedPSH LeakSafe fix.
//
// When a peer dies while holding Permission<DeadTag>, the permission
// must not silently vanish from the type system.  This header defines
// the explicit opt-in inheritance discipline used by the crash-stop
// bridge layer:
//
//   * inherits_from<DeadTag, SurvivorTag> defaults to false.
//   * Per-tag specializations opt in to a recovery edge.
//   * survivor_registry<DeadTag> is the manual registry used when a
//     caller wants permission_inherit<DeadTag>() to expand the full
//     survivor list.  C++ cannot reflect over arbitrary trait
//     specializations, so the registry is deliberately explicit.
//   * permission_inherit<DeadTag, SurvivorTags...>() mints exactly
//     the survivor Permission tokens, subject to compile-time gates.
//
// The design choice is misc/03_05_2026.md §8.1 option 1: per-tag
// opt-in.  It matches the existing CSL discipline: every region is a
// type, and ownership decisions for that region live next to its tag
// declarations.
//
// Runtime cost: zero beyond returning empty Permission tokens.
// Axiom coverage: LeakSafe, ThreadSafe, BorrowSafe.

#include <crucible/permissions/Permission.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace crucible::permissions {

template <typename... Tags>
struct inheritance_list {
    static constexpr std::size_t size = sizeof...(Tags);
};

namespace detail {

class permission_inherit_key {
    constexpr permission_inherit_key() noexcept = default;

    template <typename DeadTag, typename List>
    friend struct inherit_from_list;
};

template <typename Tag>
struct permission_inherit_minter_ {
    [[nodiscard]] static constexpr ::crucible::safety::Permission<Tag>
    mint(permission_inherit_key) noexcept {
        return ::crucible::safety::Permission<Tag>{};
    }
};

template <typename List, typename Query>
struct inheritance_list_contains;

template <typename... Tags, typename Query>
struct inheritance_list_contains<inheritance_list<Tags...>, Query>
    : std::bool_constant<(std::is_same_v<Tags, Query> || ...)> {};

template <typename List>
struct inheritance_list_empty;

template <typename... Tags>
struct inheritance_list_empty<inheritance_list<Tags...>>
    : std::bool_constant<sizeof...(Tags) == 0> {};

}  // namespace detail

// Manual survivor registry.  Specialize at the tag declaration site
// when callers need permission_inherit<DeadTag>() to expand all
// survivor tags without naming them again.
template <typename DeadTag>
struct survivor_registry {
    using type = inheritance_list<>;
};

template <typename DeadTag>
using survivors_t = typename survivor_registry<DeadTag>::type;

template <typename DeadTag, typename SurvivorTag>
struct inherits_from
    : detail::inheritance_list_contains<survivors_t<DeadTag>, SurvivorTag> {};

template <typename DeadTag, typename SurvivorTag>
inline constexpr bool inherits_from_v =
    inherits_from<DeadTag, SurvivorTag>::value;

template <typename List>
inline constexpr bool inheritance_list_empty_v =
    detail::inheritance_list_empty<List>::value;

template <typename List, typename Query>
inline constexpr bool inheritance_list_contains_v =
    detail::inheritance_list_contains<List, Query>::value;

namespace detail {

template <typename DeadTag, typename List>
struct inherit_from_list;

template <typename DeadTag, typename... SurvivorTags>
struct inherit_from_list<DeadTag, inheritance_list<SurvivorTags...>> {
    [[nodiscard]] static constexpr auto mint() noexcept {
        static_assert(sizeof...(SurvivorTags) > 0,
            "permission_inherit requires at least one survivor tag. "
            "Specialize survivor_registry<DeadTag> or pass explicit "
            "SurvivorTags.");
        static_assert((!std::is_same_v<DeadTag, SurvivorTags> && ...),
            "permission_inherit forbids circular inheritance: "
            "DeadTag cannot inherit to itself.");
        static_assert((inherits_from<DeadTag, SurvivorTags>::value && ...),
            "permission_inherit requires inherits_from<DeadTag, "
            "SurvivorTag> to be true for every survivor.");

        return std::tuple<::crucible::safety::Permission<SurvivorTags>...>{
            permission_inherit_minter_<SurvivorTags>::mint(permission_inherit_key{})...
        };
    }
};

}  // namespace detail

template <typename DeadTag, typename... SurvivorTags>
[[nodiscard]] constexpr auto permission_inherit() noexcept {
    if constexpr (sizeof...(SurvivorTags) == 0) {
        return detail::inherit_from_list<DeadTag, survivors_t<DeadTag>>::mint();
    } else {
        return detail::inherit_from_list<
            DeadTag, inheritance_list<SurvivorTags...>>::mint();
    }
}

}  // namespace crucible::permissions

namespace crucible::safety {

using ::crucible::permissions::inheritance_list;
using ::crucible::permissions::inheritance_list_contains_v;
using ::crucible::permissions::inheritance_list_empty_v;
using ::crucible::permissions::inherits_from;
using ::crucible::permissions::inherits_from_v;
using ::crucible::permissions::permission_inherit;
using ::crucible::permissions::survivor_registry;
using ::crucible::permissions::survivors_t;

}  // namespace crucible::safety
