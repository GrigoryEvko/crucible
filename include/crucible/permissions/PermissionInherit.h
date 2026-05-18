#pragma once

// ── crucible::permissions::mint_permission_inherit — crash-stop CSL recovery ──
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
//     caller wants mint_permission_inherit<DeadTag>() to expand the full
//     survivor list.  C++ cannot reflect over arbitrary trait
//     specializations, so the registry is deliberately explicit.
//   * mint_permission_inherit<DeadTag, SurvivorTags...>(crash_witness_key)
//     mints exactly the survivor Permission tokens, subject to compile-
//     time gates.  The crash_witness_key passkey is mintable ONLY from
//     bridges::wrap_crash_return — H-25 closed the hole where any caller
//     could mint survivor permissions WITHOUT proving the holder is dead.
//
// This is a registry-backed whitelist, not an algebraic lattice. There is no
// bottom/top, join/meet, monotonicity witness, or runtime provenance carrier.
// The inheritance relation is exactly the survivor_registry<DeadTag>
// specialization authored next to the peer/region tag declaration. Soundness
// depends on that specialization being reviewed as the crash-stop authority
// transfer map. Carrying runtime provenance inside Permission<Tag> would break
// the EBO-collapsed token discipline this layer protects.
//
// H-25 (proof-of-death): the survivor_registry is the STATIC map; the
// crash_witness_key parameter is the DYNAMIC proof that the bridge layer
// has executed `inner.detach(reason)` — the runtime signal that the peer
// is gone.  Bundling both gates means survivor permissions cannot be
// minted while the original holder is still alive.
//
// The design choice is misc/03_05_2026.md §8.1 option 1: per-tag
// opt-in.  It matches the existing CSL discipline: every region is a
// type, and ownership decisions for that region live next to its tag
// declarations.
//
// Runtime cost: zero beyond returning empty Permission tokens.
// crash_witness_key is an empty type — sizeof == 1, EBO-collapsed at
// every call site.
// Axiom coverage: LeakSafe, ThreadSafe, BorrowSafe.

#include <crucible/permissions/Permission.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

// ── Forward declaration: safety::proto::WrapCrashReturnKey ────────────
//
// H-25 layers crash_witness_key's gate through the EXISTING
// `WrapCrashReturnKey` passkey: WrapCrashReturnKey has a private default
// ctor friended only on `wrap_crash_return`, so only that bundler can
// mint one.  crash_witness_key takes WrapCrashReturnKey as its public
// ctor parameter — minting a crash_witness_key therefore requires
// holding a WrapCrashReturnKey, which transitively requires being
// `wrap_crash_return`.
//
// This sidesteps the cross-namespace templated-friend matching trap (a
// `friend safety::proto::wrap_crash_return` declared inside
// `crucible::permissions::crash_witness_key` is rejected by GCC 16
// because qualified-name template-id friend matching is brittle across
// namespace boundaries).
//
// CrashTransport.h is the consumer; permissions/PermissionInherit.h is a
// dependency of it, so the dependency direction is preserved (we never
// #include CrashTransport.h from here, only forward-declare).
namespace crucible::safety::proto {
class WrapCrashReturnKey;
}  // namespace crucible::safety::proto

namespace crucible::permissions {

// ── crash_witness_key — H-25 proof-of-death passkey ───────────────────
//
// Empty class.  The only public ctor takes a
// `crucible::bridges::WrapCrashReturnKey` — that class's own default
// ctor is private and friended only to `wrap_crash_return`, so the
// transitive gate is:
//
//   wrap_crash_return<PeerTag>(inner, reason, resource)
//      │
//      ├─ inner.detach(reason)               // proof-of-death (dynamic)
//      ├─ WrapCrashReturnKey{}               // friend access — bridges only
//      ├─ crash_witness_key{wrap_key}        // public ctor takes wrap_key
//      └─ mint_permission_inherit<PeerTag>(witness_key)
//             │
//             └─ returns survivor Permission tokens
//
// Direct user code attempting `pi::crash_witness_key{}` fails because
// the default ctor is deleted (no matching constructor).  Attempting
// `pi::crash_witness_key{bridges::WrapCrashReturnKey{}}` fails because
// `WrapCrashReturnKey{}` is private.  Both paths closed — the H-25
// hole where any TU could call `mint_permission_inherit<DeadTag>()`
// from anywhere while the legitimate holder was still alive.
//
// EBO-collapsed at every call site: sizeof(crash_witness_key) == 1 but
// the parameter type is [[no_unique_address]]-compatible in callee
// context, so the runtime cost of the gate is zero.
class crash_witness_key {
public:
    crash_witness_key() = delete;
    // Public ctor requires a WrapCrashReturnKey value.  WrapCrashReturnKey
    // has a private default ctor friended only on wrap_crash_return, so
    // only safety::proto::wrap_crash_return can construct this argument
    // and therefore only it can construct crash_witness_key.  Passed by
    // const-ref so this header can use the forward-declared type without
    // requiring its complete definition (CrashTransport.h supplies that).
    explicit constexpr crash_witness_key(
        ::crucible::safety::proto::WrapCrashReturnKey const&) noexcept {}
};

template <typename... Tags>
struct inheritance_list {
    static constexpr std::size_t size = sizeof...(Tags);
};

namespace detail {

class mint_permission_inherit_key {
    constexpr mint_permission_inherit_key() noexcept = default;

    template <typename DeadTag, typename List>
    friend struct inherit_from_list;
};

template <typename Tag>
struct mint_permission_inherit_minter_ {
    [[nodiscard]] static constexpr ::crucible::safety::Permission<Tag>
    mint(mint_permission_inherit_key) noexcept {
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
// when callers need mint_permission_inherit<DeadTag>() to expand all
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
            "mint_permission_inherit requires at least one survivor tag. "
            "Specialize survivor_registry<DeadTag> or pass explicit "
            "SurvivorTags.");
        static_assert((!std::is_same_v<DeadTag, SurvivorTags> && ...),
            "mint_permission_inherit forbids circular inheritance: "
            "DeadTag cannot inherit to itself.");
        static_assert((inherits_from<DeadTag, SurvivorTags>::value && ...),
            "mint_permission_inherit requires inherits_from<DeadTag, "
            "SurvivorTag> to be true for every survivor.");

        return std::tuple<::crucible::safety::Permission<SurvivorTags>...>{
            mint_permission_inherit_minter_<SurvivorTags>::mint(
                mint_permission_inherit_key{})...
        };
    }
};

}  // namespace detail

// H-25: the trailing `crash_witness_key` parameter is the proof-of-death
// passkey.  Only bridges::wrap_crash_return can mint one, so direct user
// callers cannot reach this function: passing `crash_witness_key{}`
// from arbitrary scope fails (private ctor, unfriended).  See key class
// doc above for the full flow rationale.
template <typename DeadTag, typename... SurvivorTags>
[[nodiscard]] constexpr auto mint_permission_inherit(
    crash_witness_key) noexcept
{
    if constexpr (sizeof...(SurvivorTags) == 0) {
        return detail::inherit_from_list<DeadTag, survivors_t<DeadTag>>::mint();
    } else {
        return detail::inherit_from_list<
            DeadTag, inheritance_list<SurvivorTags...>>::mint();
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
using ::crucible::permissions::survivor_registry;
using ::crucible::permissions::survivors_t;

}  // namespace crucible::safety
