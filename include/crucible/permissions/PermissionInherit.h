#pragma once

// When a peer dies holding a permission, that authority must not simply
// vanish from the type system.  Recovery here is an explicit opt-in: a
// dead tag names the tags that inherit its authority, and nothing else
// can claim it.
//
// The relation is a hand-written whitelist, not a lattice.  There is no
// join, no meet, and no provenance carried at runtime, because a token
// that carried provenance would not collapse to nothing in the types
// that hold it.  C++ also cannot enumerate the specializations of
// a trait, so the registry has to be written out rather than derived
// from the individual edges.
//
// Two gates, and both are needed.  The registry says statically who may
// inherit.  The witness key says dynamically that the holder is in fact
// gone, so survivors cannot be minted underneath a peer that is still
// alive.

#include <crucible/permissions/Permission.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

// The witness key's gate is layered on this existing passkey rather than
// on a direct friend declaration, because a friend declaration naming a
// qualified template-id across a namespace boundary does not match
// reliably.  Taking the other passkey as a constructor parameter gets the
// same restriction with ordinary access control.
namespace crucible::safety::proto {
class WrapCrashReturnKey;
}  // namespace crucible::safety::proto

namespace crucible::permissions {

class crash_witness_key {
public:
    crash_witness_key() = delete;
    // The parameter cannot be constructed by an arbitrary caller: its own
    // default constructor is private, reachable only from the bridge that
    // detaches a dead peer.  Holding one is therefore the proof of death.
    // It comes by const-ref so this header needs only the forward
    // declaration of that type.
    explicit constexpr crash_witness_key(::crucible::safety::proto::WrapCrashReturnKey const&) noexcept {}
};

template <typename... Tags>
struct inheritance_list {};

namespace detail {

class mint_permission_inherit_key {
    constexpr mint_permission_inherit_key() noexcept = default;

    template <typename DeadTag, typename List>
    friend struct inherit_from_list;
};

template <typename Tag>
struct mint_permission_inherit_minter_ {
    [[nodiscard]] static constexpr ::crucible::safety::Permission<Tag> mint(mint_permission_inherit_key) noexcept {
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
struct inheritance_list_empty<inheritance_list<Tags...>> : std::bool_constant<sizeof...(Tags) == 0> {};

}  // namespace detail

// The authority-transfer map for crash-stop recovery.  Specialize it
// beside the tag declarations it names, where a reviewer reading those
// tags also reads who inherits from them.
template <typename DeadTag>
struct survivor_registry {
    using type = inheritance_list<>;
};

template <typename DeadTag>
using survivors_t = typename survivor_registry<DeadTag>::type;

template <typename DeadTag, typename SurvivorTag>
struct inherits_from : detail::inheritance_list_contains<survivors_t<DeadTag>, SurvivorTag> {};

template <typename DeadTag, typename SurvivorTag>
inline constexpr bool inherits_from_v = inherits_from<DeadTag, SurvivorTag>::value;

template <typename List>
inline constexpr bool inheritance_list_empty_v = detail::inheritance_list_empty<List>::value;

template <typename List, typename Query>
inline constexpr bool inheritance_list_contains_v = detail::inheritance_list_contains<List, Query>::value;

namespace detail {

template <typename DeadTag, typename List>
struct inherit_from_list;

template <typename DeadTag, typename... SurvivorTags>
struct inherit_from_list<DeadTag, inheritance_list<SurvivorTags...>> {
    [[nodiscard]] static constexpr std::tuple<::crucible::safety::Permission<SurvivorTags>...> mint() noexcept {
        return std::tuple<::crucible::safety::Permission<SurvivorTags>...>{
            mint_permission_inherit_minter_<SurvivorTags>::mint(mint_permission_inherit_key{})...};
    }
};

template <typename DeadTag, typename... SurvivorTags>
using mint_permission_inherit_list_t =
    std::conditional_t<sizeof...(SurvivorTags) == 0, survivors_t<DeadTag>, inheritance_list<SurvivorTags...>>;

// The gates live in the return type rather than in the minting function
// body, so they fire while the signature is instantiated.  A body is not
// instantiated during overload resolution, so an assert placed there
// would stay silent on exactly the malformed calls these catch.
template <typename DeadTag, typename List>
struct validated_perm_tuple;

template <typename DeadTag, typename... Survivors>
struct validated_perm_tuple<DeadTag, inheritance_list<Survivors...>> {
    static_assert(sizeof...(Survivors) > 0, "mint_permission_inherit requires at least one survivor tag. "
                                            "Specialize survivor_registry<DeadTag> or pass explicit "
                                            "SurvivorTags.");
    static_assert((!std::is_same_v<DeadTag, Survivors> && ...), "mint_permission_inherit forbids circular inheritance: "
                                                                "DeadTag cannot inherit to itself.");
    static_assert((inherits_from<DeadTag, Survivors>::value && ...),
                  "mint_permission_inherit requires inherits_from<DeadTag, "
                  "SurvivorTag> to be true for every survivor.");

    using type = std::tuple<::crucible::safety::Permission<Survivors>...>;
};

}  // namespace detail

template <typename DeadTag, typename... SurvivorTags>
using mint_permission_inherit_t =
    typename detail::validated_perm_tuple<DeadTag,
                                          detail::mint_permission_inherit_list_t<DeadTag, SurvivorTags...>>::type;

template <typename DeadTag, typename... SurvivorTags>
[[nodiscard]] constexpr mint_permission_inherit_t<DeadTag, SurvivorTags...>
mint_permission_inherit(crash_witness_key) noexcept {
    if constexpr (sizeof...(SurvivorTags) == 0) {
        return detail::inherit_from_list<DeadTag, survivors_t<DeadTag>>::mint();
    } else {
        return detail::inherit_from_list<DeadTag, inheritance_list<SurvivorTags...>>::mint();
    }
}

}  // namespace crucible::permissions

namespace crucible::safety {

using ::crucible::permissions::crash_witness_key;
using ::crucible::permissions::inheritance_list;
using ::crucible::permissions::inheritance_list_contains_v;
using ::crucible::permissions::inheritance_list_empty_v;
using ::crucible::permissions::inherits_from;
using ::crucible::permissions::inherits_from_v;
using ::crucible::permissions::mint_permission_inherit;
using ::crucible::permissions::mint_permission_inherit_t;
using ::crucible::permissions::survivor_registry;
using ::crucible::permissions::survivors_t;

}  // namespace crucible::safety
