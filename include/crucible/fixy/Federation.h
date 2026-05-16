#pragma once

// ── crucible::fixy — Federation.h (FIXY-G7, CAPSTONE) ─────────────────
//
// Federation grade-intersection.  When N peer organizations participate
// in a federated computation (DiLoCo / cross-org training / Canopy
// gossip), the shared message-grade is the axis-wise meet of each
// peer's local grade — the most permissive shared posture every role
// can honor.  An empty intersection (no shared posture exists) rejects
// at the federation-channel mint site.
//
// **Surface.**
//
//   fixy::FederationGrade<RoleGrades...>     — meet-projection metafunction.
//   fixy::mint_federation_channel<Roles...>(role_handles...)
//                                            — universal-mint factory
//                                              producing an MPST channel.
//   fixy::FederationEmpty<Roles...>          — diagnostic carrier when
//                                              the meet is empty.
//
// **Mechanism.**  For each dim, compute the per-role grade value;
// federation-grade is the meet (most-permissive shared value).  When
// roles disagree on a STRICT axis (e.g., one role pins
// vendor_nv, another pins vendor_am), the meet is empty and the
// mint rejects.
//
// **Scope.**  Phase G7 ships pairwise intersection for the load-bearing
// axes (Vendor, Trust, NumericalTier).  Other axes default to "must
// match exactly" — equality-based meet.  Future iterations install
// proper lattice meets per axis (e.g., Trust::Unverified ⊓ Trust::
// Verified = Trust::Unverified for the meet ordering).
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — FederationChannel default-constructs from role pack.
//   TypeSafe   — Roles are typed-Fn instances; concept-gated.
//   NullSafe   — empty intersection is a compile error, not a silent slot.
//   MemSafe    — pure type-level metafunction; no allocation.
//   BorrowSafe — channel handle wraps role handles by value.
//   ThreadSafe — no shared state.
//   LeakSafe   — channel handle is a value-type aggregate.
//   DetSafe    — meet computation is purely type-driven.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §5 Phase G    — federation grade-intersection
//   fixy/Flow.h                           — companion cross-binding algebra
//   fixy/WireGrade.h                      — wire-format companion

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reflect.h>
#include <crucible/fixy/Reject.h>

#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── FederationEmpty diagnostic carrier ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Roles>
struct FederationEmpty {
    // Type identity carries the diagnostic; intentionally empty body.
};

// ═════════════════════════════════════════════════════════════════════
// ── Per-axis meet (concept-gated) ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// Pairwise check across all roles: every pair must agree on the axis,
// OR one role's grade refines the other (trust::Verified refines
// trust::Unverified, etc.).  For Phase G7 we ship equality-based meet
// on Vendor, Trust, NumericalTier; other axes also enforce equality.

template <typename A, typename B>
inline constexpr bool repr_compatible_v = (A::repr_v == B::repr_v);

template <typename A, typename B>
inline constexpr bool trust_compatible_v =
    std::is_same_v<typename A::trust_t, typename B::trust_t>;

template <typename A, typename B>
inline constexpr bool effect_row_compatible_v =
    std::is_same_v<typename A::effect_row_t, typename B::effect_row_t>;

template <typename A, typename B>
inline constexpr bool precision_compatible_v =
    std::is_same_v<typename A::precision_t, typename B::precision_t>;

template <typename A, typename B>
inline constexpr bool federation_pair_compatible_v =
    repr_compatible_v<A, B>      &&
    trust_compatible_v<A, B>     &&
    effect_row_compatible_v<A, B> &&
    precision_compatible_v<A, B>;

// All-pairs check across a Role pack.
template <typename... Roles>
struct all_pairs_compatible;

template <>
struct all_pairs_compatible<> : std::true_type {};

template <typename R>
struct all_pairs_compatible<R> : std::true_type {};

template <typename R1, typename... Rest>
struct all_pairs_compatible<R1, Rest...> {
    static constexpr bool value =
        (federation_pair_compatible_v<R1, Rest> && ...) &&
        all_pairs_compatible<Rest...>::value;
};

template <typename... Roles>
inline constexpr bool all_pairs_compatible_v =
    all_pairs_compatible<std::remove_cvref_t<Roles>...>::value;

}  // namespace detail

template <typename... Roles>
concept FederationCompatible =
    (IsFixyFn<Roles> && ...) &&
    detail::all_pairs_compatible_v<Roles...>;

// ═════════════════════════════════════════════════════════════════════
// ── FederationGrade<Roles...> ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Roles>
struct FederationGrade {
    static_assert(FederationCompatible<Roles...>,
        "FederationEmpty: at least two roles disagree on a load-bearing "
        "axis (Vendor/Trust/Effect/Precision).  No shared posture exists "
        "— mint_federation_channel rejects.  Reconcile the roles' grade "
        "declarations before retrying.");

    // The meet IS the first role's grade (all roles agreed by the
    // concept gate, so picking any one is sound).  Documented choice:
    // first-listed role is the federation's canonical view.
    using meet_role_t =
        std::tuple_element_t<0, std::tuple<std::remove_cvref_t<Roles>...>>;
};

// ═════════════════════════════════════════════════════════════════════
// ── FederationChannel<Roles...> ────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Roles>
struct FederationChannel final {
    using grade_t = FederationGrade<Roles...>;
    using meet_role_t = typename grade_t::meet_role_t;

    std::tuple<Roles...> roles{};

    constexpr FederationChannel() = default;
    constexpr explicit FederationChannel(Roles... rs)
        : roles{std::move(rs)...} {}
};

// ═════════════════════════════════════════════════════════════════════
// ── mint_federation_channel — universal mint pattern ───────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Roles>
    requires FederationCompatible<Roles...>
[[nodiscard]] constexpr FederationChannel<std::remove_cvref_t<Roles>...>
mint_federation_channel(Roles... roles) noexcept
{
    return FederationChannel<std::remove_cvref_t<Roles>...>{
        std::forward<Roles>(roles)...};
}

}  // namespace crucible::fixy
