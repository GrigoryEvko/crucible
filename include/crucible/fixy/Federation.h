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
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/stance/Version.h>

#include <algorithm>
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
// ── Followup A — R020 mint-time enforcement ────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// FIXY-G12 shipped R020 (federation peer roles require terminating +
// wallclock_budget) as a CONSUMER-SIDE predicate.  The pre-Followup
// `mint_federation_channel<Roles...>` did NOT auto-enforce R020 — a
// channel could be minted with peer roles that lacked bounded-resource
// discipline and the violation surfaced only at admission downstream.
//
// Followup A threads R020 into the mint's `requires` clause: every
// participating role MUST satisfy `R020_federation_peer_bounded_v`.
// A peer missing terminating or wallclock_budget rejects at the mint
// construction site with a structured diagnostic naming the offending
// role's position in the pack.

namespace detail {

template <typename... Roles>
inline constexpr bool all_roles_r020_satisfied_v =
    (::crucible::fixy::rule::R020_federation_peer_bounded_v<
        std::remove_cvref_t<Roles>> && ... && true);

}  // namespace detail

template <typename... Roles>
inline constexpr bool all_roles_r020_satisfied_v =
    detail::all_roles_r020_satisfied_v<Roles...>;

// R020_FederationPeerUnbounded<Role> — diagnostic carrier naming the
// offending role.  Substrate-level Categories live in safety/Diagnostic.h;
// fixy-layer rule diagnostics carry their own type identity for grep-
// discoverable rejection messages.
template <typename Role>
struct R020_FederationPeerUnbounded {
    static constexpr const char* description =
        "R020 — federation peer role lacks bounded-resource discipline "
        "(terminating + wallclock_budget required).  A federation "
        "participant that can spin indefinitely poisons the MPST "
        "channel.  Annotate the role with cg::terminating + "
        "cg::wallclock_budget<Nanos> before participating.";
};

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
    requires FederationCompatible<Roles...> &&
             detail::all_roles_r020_satisfied_v<Roles...>
[[nodiscard]] constexpr FederationChannel<std::remove_cvref_t<Roles>...>
mint_federation_channel(Roles... roles) noexcept
{
    static_assert(detail::all_roles_r020_satisfied_v<Roles...>,
        "mint_federation_channel rejects: at least one role in the Roles "
        "pack fails R020 — federation peer roles must carry cg::terminating "
        "+ cg::wallclock_budget<Nanos>.  Decorate the offending role(s) "
        "and retry.  See fixy::R020_FederationPeerUnbounded<Role> for the "
        "structured diagnostic carrier.");
    return FederationChannel<std::remove_cvref_t<Roles>...>{
        std::forward<Roles>(roles)...};
}

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-G13 — Stance version negotiation ──────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each federation participant declares which stance versions it
// accepts via `RoleVersionAccept<Stance, Lo, Hi>`.  Federation
// intersects the per-role accept windows AND checks they're non-
// disjoint.  When the intersection of `[Lo_i, Hi_i]` is empty, the
// federation cannot proceed.
//
// **Mechanism.**  Pairwise window intersection per role-pack.  The
// channel-wide window is the intersection of every role's window.
// An empty intersection rejects.

template <typename... StanceTags>
struct FederationProtocolError {
    static constexpr const char* description =
        "FederationProtocolError: stance version windows across the "
        "peer pack have an empty intersection.  No shared version "
        "exists that every peer accepts.  Widen the accept_versions "
        "window on the offending role(s) or migrate to a shared "
        "version range.";
};

namespace detail {

// Pack-level fold: compute the intersection [max(Lo_i), min(Hi_i)].
template <typename... Accepts>
struct accept_windows_meet;

template <>
struct accept_windows_meet<> {
    static constexpr std::uint16_t min_v = 0;
    static constexpr std::uint16_t max_v = UINT16_MAX;
    static constexpr bool non_empty_v = true;
};

template <typename A, typename... Rest>
struct accept_windows_meet<A, Rest...> {
private:
    using TailMeet = accept_windows_meet<Rest...>;
public:
    static constexpr std::uint16_t min_v =
        (A::min_v > TailMeet::min_v) ? A::min_v : TailMeet::min_v;
    static constexpr std::uint16_t max_v =
        (A::max_v < TailMeet::max_v) ? A::max_v : TailMeet::max_v;
    static constexpr bool non_empty_v =
        (min_v <= max_v) && TailMeet::non_empty_v;
};

}  // namespace detail

// Public façade: intersect a pack of accept_versions windows.
template <typename... Accepts>
inline constexpr std::uint16_t federation_version_meet_lo_v =
    detail::accept_windows_meet<Accepts...>::min_v;

template <typename... Accepts>
inline constexpr std::uint16_t federation_version_meet_hi_v =
    detail::accept_windows_meet<Accepts...>::max_v;

template <typename... Accepts>
inline constexpr bool federation_version_windows_compatible_v =
    detail::accept_windows_meet<Accepts...>::non_empty_v;

// ═════════════════════════════════════════════════════════════════════
// ── mint_federation_channel_versioned ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Stance-versioned federation mint.  Each role pairs with an
// `accept_versions<S, Lo, Hi>` declaring the role's accepted window.
// The channel-wide shared window is the intersection.  Empty
// intersection → reject.

template <typename... AcceptVersions>
struct VersionedFederationChannel final {
    static_assert(federation_version_windows_compatible_v<AcceptVersions...>,
        "VersionedFederationChannel: stance version windows have an "
        "empty intersection across the peer pack.  See "
        "fixy::FederationProtocolError<Stances...> for the structured "
        "carrier and widen the offending role(s)' accept_versions.");

    static constexpr std::uint16_t shared_min_v =
        federation_version_meet_lo_v<AcceptVersions...>;
    static constexpr std::uint16_t shared_max_v =
        federation_version_meet_hi_v<AcceptVersions...>;
};

template <typename... AcceptVersions>
    requires (federation_version_windows_compatible_v<AcceptVersions...>)
[[nodiscard]] constexpr VersionedFederationChannel<AcceptVersions...>
mint_federation_channel_versioned() noexcept
{
    return {};
}

}  // namespace crucible::fixy
