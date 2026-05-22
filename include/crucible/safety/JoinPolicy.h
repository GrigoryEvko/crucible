#pragma once

// ── crucible::safety::JoinPolicy<JoinPolicy_v Tier, T> ─────────────
//
// Type-pinned fork-join engagement wrapper.  A value of type T whose
// structural-concurrency engagement tier (FORGET ⊑ DETACH ⊑ ABANDON
// ⊑ CANCEL ⊑ WAIT_DEADLINE ⊑ JOIN_ALL) is fixed at the type level via
// the non-type template parameter Tier.  Carrier for the V-078..V-082
// substrate arc — V-078 ships the lattice, V-079 (this header) the
// Graded carrier + row_hash specialization + IsJoinPolicy concept,
// V-080..V-082 cover ThreadLocalRef + CollisionCatalog rules,
// V-083/V-203 wire `mint_spawn` / fixy::spawn re-exports.
//
//   Substrate: Graded<ModalityKind::Comonad,
//                     JoinPolicyLattice::At<Tier>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//                 empty, sizeof(JoinPolicy<Tier, T>) == sizeof(T))
//
//   Use case:  `permission_fork<Children...>(parent, callables...)`
//              returns a `JoinPolicy<JOIN_ALL, ChildrenTuple>` —
//              the type-level claim "every child completed before
//              this returned" is verifiable by inspection of the
//              produced wrapper, NOT by reading source.  A consumer
//              site that only requires CANCEL semantics accepts a
//              JOIN_ALL-tagged result via relax<CANCEL>().  Future
//              looser-engagement spawns (V-083 mint_spawn variants)
//              return weaker tiers — FORGET for fire-and-forget
//              workers, DETACH for caller-released handles, etc.
//
//   Axiom coverage:
//     TypeSafe — JoinPolicy_v is a strong enum (`enum class : uint8_t`);
//                cross-tier mismatches are compile errors via the
//                relax<WeakerTier>() and satisfies<RequiredTier> gates.
//     DetSafe — every operation is constexpr; the tier is a STATIC
//                property of the wrapper, so cross-vendor CI's
//                BITEXACT_STRICT recipe-tier matrix validates per-tier
//                invariants identically across hardware.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//                JoinPolicy IS COPYABLE — the policy is metadata about
//                the parent's engagement claim, NOT a classified-
//                information channel restricting duplication (cf.
//                Secret which deletes copy).
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(JoinPolicy<Tier, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Tier>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Comonad ───────────────────────────────────────────
//
// A join-policy tier encodes "the parent had this much structural-
// concurrency engagement when the value was produced."  The substrate's
// natural Comonad-counit operation is `extract`: observe the engaged
// value as plain T.  This is sound BY DESIGN — the policy adds CLAIM
// about how the parent waited (or chose not to wait), not RESTRICTION
// on observation.  A consumer reading a `JoinPolicy<JOIN_ALL, T>` is
// observing the SAME T bytes a consumer reading a `JoinPolicy<FORGET,
// T>` would observe; the only difference is the type-level claim
// about the parent's engagement.
//
// This is the dual of Secret<T>: Secret's Comonad-counit is GATED via
// the `secret_policy::*` declassify rail (information-flow exit
// restriction); JoinPolicy's Comonad-counit is UNGATED (observation
// is sound by construction).  Mirrors WitnessLattice's choice exactly
// — both encode "metadata about producer's discipline" rather than
// "restriction on consumer's access."
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// JoinPolicy subsumption-direction (per JoinPolicyLattice.h L89-107):
//
//   leq(loose, strict) reads "the looser tier is subsumed by the
//   stricter tier."  Bottom = FORGET (loosest); Top = JOIN_ALL
//   (strictest).
//
// For USE, the direction is REVERSED:
//
//   A producer at a HIGHER tier (JOIN_ALL) satisfies a consumer at a
//   LOWER tier (FORGET / DETACH / ABANDON / CANCEL / WAIT_DEADLINE).
//   Stricter engagement serves weaker requirement.  A `JoinPolicy<
//   JOIN_ALL, T>` can be relaxed to `JoinPolicy<CANCEL, T>` — the
//   region that joined every child trivially satisfies "at minimum,
//   cancel on exit."
//
//   The converse is forbidden: a `JoinPolicy<DETACH, T>` CANNOT
//   become a `JoinPolicy<JOIN_ALL, T>` — a region that already
//   released its children CANNOT post-hoc claim it joined them.  No
//   `tighten()` method exists; the only way to obtain a stricter-tier
//   policy is to construct one at a SITE that actually performed the
//   engagement (e.g., a `permission_fork` body that did wait for
//   every jthread to join).
//
// API:
//
//   - extract() &&                 — Comonad counit; returns T,
//                                    erases the tier.  The named
//                                    "engaged → raw" path.
//   - peek() / peek_mut() / consume() — same as every Graded-backed
//                                    wrapper; observe / mutate / move.
//   - relax<WeakerTier>() &  / &&  — convert to a less-strict tier;
//                                    compile error if WeakerTier >
//                                    Tier.
//   - satisfies<RequiredTier>      — static predicate: does this
//                                    wrapper's pinned tier subsume
//                                    the required tier?  Equivalent
//                                    to leq(RequiredTier, Tier).
//   - tier (static constexpr)      — the pinned JoinPolicy_v value.
//                                    Spelled `tier` (matches Witness's
//                                    proof-strength vocabulary, since
//                                    engagement strictness is also a
//                                    tiered property).
//
// `Graded::weaken` on the substrate goes UP the lattice (stricter
// engagement) — that operation has no meaningful semantics for a
// type-pinned tier and is hidden by the wrapper.  The wrapper exposes
// only relax (DOWN the lattice; looser tier still served by the
// stricter engagement).
//
// See V-078 (#1956, JoinPolicyLattice.h) for the underlying lattice;
// V-081/V-082 (#1959/#1960) for CollisionCatalog × Wait/Bg rules;
// V-083 (#1961) for `mint_spawn` / `mint_parallel_for` factories;
// V-203 (#2081) for the fixy::spawn::JoinPolicy phantom-tag re-export.
//
// ── §XXI Universal Mint factory ─────────────────────────────────────
//
// `mint_join_policy<Tier, T>(args...)` synthesizes a `JoinPolicy<
// Tier, T>` at the §XXI grep-discoverable boundary.  Per CLAUDE.md
// §XXI: every authorization factory is named `mint_<noun>` so
// `grep "mint_"` finds every site that explicitly opts into the
// tier-level engagement claim.  Constructing `JoinPolicy<Tier, T>{
// value}` directly is functionally equivalent — both gate on
// `std::is_constructible_v<T, Args...>` — the §XXI mint exists for
// grep-discoverability AND because policy mints are the SOUND moment
// where the producer must have already performed the engagement
// (the substrate cannot verify "did the parent actually wait?";
// the discipline is at the producer's mint site).
//
// HS14 gate: two HS14 neg-compile fixtures at test/safety_neg/
// witness the gate fires across distinct mismatch classes:
//   1. relax-to-stricter — neg_join_policy_relax_to_stricter.cpp
//      witnesses the relax<StricterTier>() requires-clause rejects
//      the upward conversion (mirrors Witness's same-direction gate
//      across a NEW lattice — the substrate carries both the lattice
//      and the wrapper through the same compile-time gate).
//   2. wrong-arg-type — neg_join_policy_mint_wrong_arg.cpp witnesses
//      mint_join_policy<TIER, T>(bad_arg) requires-clause rejects
//      when T is not constructible from the supplied args.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/JoinPolicyLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the JoinPolicyLattice enum into the safety:: namespace so
// call sites don't need to spell `algebra::lattices::JoinPolicy::JOIN_ALL`.
// Matches Witness.h's `Witness_v` aliasing — the class template
// `JoinPolicy<Tier, T>` shadows the lattice's `JoinPolicy` enum in
// `safety::` scope, so we re-export the enum under the unambiguous
// alias `JoinPolicy_v` ("join-policy value").  Production call sites
// write `JoinPolicy<JoinPolicy_v::JOIN_ALL, T>` for clarity; the
// alias namespace `join_policy_tier::JoinAll<T>` etc. provides
// shorter forms for common cases.
using ::crucible::algebra::lattices::JoinPolicyLattice;
using JoinPolicy_v = ::crucible::algebra::lattices::JoinPolicy;

template <JoinPolicy_v Tier, typename T>
class [[nodiscard]] JoinPolicy {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = JoinPolicyLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Comonad,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Comonad;

    // The pinned tier — exposed as a static constexpr for callers
    // doing tier-aware dispatch without instantiating the wrapper.
    // Spelled `tier` (matches Witness.h's vocabulary; engagement
    // strictness is a tiered property like proof strength).
    static constexpr JoinPolicy_v tier = Tier;

private:
    // Empty-lattice element_type collapses via [[no_unique_address]]
    // in Graded; impl_ is byte-equivalent to T at -O3.
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned tier.
    //
    // SEMANTIC NOTE: a default-constructed JoinPolicy<JOIN_ALL, T>
    // claims its T{} bytes were produced under full structural-
    // concurrency engagement.  For trivially-zero T this is vacuously
    // a metadata claim about the producer's process; for non-trivial
    // T, the claim becomes meaningful only if the wrapper is
    // constructed in a context that genuinely performed the engagement.
    // Production callers SHOULD prefer the explicit-T constructor (or
    // mint_join_policy factory) at sites that have actually performed
    // the join — the default ctor exists for compatibility with
    // std::array<JoinPolicy<...>, N> / struct-field default-init
    // contexts.
    constexpr JoinPolicy() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a fork-join site produces a value
    // alongside the engagement discipline; the wrapper binds that
    // tier into the type.
    constexpr explicit JoinPolicy(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction — avoids moving T through a temporary.
    // Mirrors Witness.h's std::in_place_t pattern.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit JoinPolicy(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — JoinPolicy IS COPYABLE.  A tier
    // pin is a static property of the value's engagement strictness;
    // copying a value copies its policy claim unchanged.  This is
    // the OPPOSITE of Secret<T>, which deletes copy to enforce
    // information-flow non-duplication — policy IS metadata, not a
    // classified channel.
    constexpr JoinPolicy(const JoinPolicy&)            = default;
    constexpr JoinPolicy(JoinPolicy&&)                 = default;
    constexpr JoinPolicy& operator=(const JoinPolicy&) = default;
    constexpr JoinPolicy& operator=(JoinPolicy&&)      = default;
    ~JoinPolicy()                                      = default;

    // Equality: compares value bytes within the SAME tier pin.
    // Cross-tier comparison is rejected at overload resolution
    // because the friend takes two `JoinPolicy const&` of identical
    // <Tier, T> instantiation.  Mirrors Witness.h's family-parity
    // discipline.
    [[nodiscard]] friend constexpr bool operator==(
        JoinPolicy const& a, JoinPolicy const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via P2996 reflection.
    // lattice_name():    "JoinPolicyLattice::At<JOIN_ALL>" etc.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    // ── Mutable access ──────────────────────────────────────────────
    //
    // peek_mut forwards through Graded::peek_mut — admitted by the
    // refined gate `(AbsoluteModality || empty grade)`: At<Tier>::
    // element_type is empty for every Tier, so Comonad+empty admits
    // the operation.  Mutating T cannot violate the tier pin: the
    // tier is a TYPE-LEVEL fact about how the value was produced,
    // not about the value's current bytes.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── extract — Comonad counit ────────────────────────────────────
    //
    // The named "engaged → raw" path.  Consumes the JoinPolicy and
    // returns the underlying T; the tier metadata is erased.
    // Unrestricted (unlike Secret<T>::declassify, which requires a
    // policy tag) — observing an engaged value as plain T is sound
    // because the policy adds CLAIM about producer's discipline, not
    // RESTRICTION on observation.  Forwards through Graded::extract().
    //
    // Symmetric naming with Witness::extract and Secret::declassify
    // (all three are Comonad counits, all three are `&&`-qualified,
    // all three return T): the methods do the same thing at the
    // substrate level; the asymmetry is at the gate — declassify
    // requires a `secret_policy::*` tag; extract is open.
    [[nodiscard]] constexpr T extract() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).extract();
    }

    // ── swap (forwarded from Graded substrate) ─────────────────────
    constexpr void swap(JoinPolicy& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(JoinPolicy& a, JoinPolicy& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredTier> — static subsumption check ─────────
    //
    // True iff this wrapper's pinned tier is at least as strict as
    // RequiredTier.  Implements the lattice direction:
    //
    //   leq(RequiredTier, Tier)  reads
    //   "the required tier is BELOW (or equal to) the pinned tier"
    //
    // — which means the pinned tier subsumes the requirement; the
    // pinned-tier value is admissible at the consumer site.
    //
    // Use:
    //   static_assert(JoinPolicy<JoinPolicy_v::JOIN_ALL, T>
    //                     ::satisfies<JoinPolicy_v::CANCEL>);
    //   // ✓ — JOIN_ALL subsumes CANCEL (joined every child ⇒ also
    //   //     cancellable on exit)
    //
    //   static_assert(!JoinPolicy<JoinPolicy_v::DETACH, T>
    //                      ::satisfies<JoinPolicy_v::JOIN_ALL>);
    //   // ✓ — DETACH does NOT subsume JOIN_ALL (released children
    //   //     cannot post-hoc satisfy joined-them requirement)
    template <JoinPolicy_v RequiredTier>
    static constexpr bool satisfies = JoinPolicyLattice::leq(RequiredTier, Tier);

    // ── relax<WeakerTier> — convert to a less-strict tier ──────────
    //
    // Returns a JoinPolicy<WeakerTier, T> carrying the same value
    // bytes.  Allowed iff WeakerTier ≤ Tier in the lattice (the
    // weaker tier is below or equal to the pinned tier).  A
    // stricter engagement still satisfies a weaker requirement.
    //
    // Compile error when WeakerTier > Tier — there's no way to
    // strengthen a tier pin once the value was produced under a
    // weaker engagement regime.
    template <JoinPolicy_v WeakerTier>
        requires (JoinPolicyLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr JoinPolicy<WeakerTier, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return JoinPolicy<WeakerTier, T>{this->peek()};
    }

    template <JoinPolicy_v WeakerTier>
        requires (JoinPolicyLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr JoinPolicy<WeakerTier, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return JoinPolicy<WeakerTier, T>{
            std::move(impl_).consume()};
    }
};

// ── §XXI Universal Mint factory ─────────────────────────────────────
//
// Token mint (no Ctx) per §XXI table.  JoinPolicy is type-tagged by
// `Tier`; the mint forwards constructor args through the in-place
// path, the requires-clause gates on `std::is_constructible_v<T,
// Args...>` (load-bearing soundness check).
template <JoinPolicy_v Tier, typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr JoinPolicy<Tier, T> mint_join_policy(Args&&... args)
    noexcept(std::is_nothrow_constructible_v<T, Args...>)
{
    return JoinPolicy<Tier, T>{std::in_place, std::forward<Args>(args)...};
}

// ── Convenience aliases ─────────────────────────────────────────────
//
// Per-tier aliases for the most common production sites.  Mirrors
// the witness_tier:: namespace in Witness.h.
//
// NAMING NOTE: the namespace is `join_policy_tier` (NOT bare
// `join_policy`) to keep this V-079 carrier's tier-pinned shortcuts
// separate from any V-203 fixy::spawn::JoinPolicy phantom-tag tree
// that might land at a parallel-vocabulary site, AND to keep the
// vocabulary parallel with Witness.h's `witness_tier::` convention.
namespace join_policy_tier {
    template <typename T> using Forget        = JoinPolicy<JoinPolicy_v::FORGET,        T>;
    template <typename T> using Detach        = JoinPolicy<JoinPolicy_v::DETACH,        T>;
    template <typename T> using Abandon       = JoinPolicy<JoinPolicy_v::ABANDON,       T>;
    template <typename T> using Cancel        = JoinPolicy<JoinPolicy_v::CANCEL,        T>;
    template <typename T> using WaitDeadline  = JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, T>;
    template <typename T> using JoinAll       = JoinPolicy<JoinPolicy_v::JOIN_ALL,      T>;
}  // namespace join_policy_tier

// ── Layout invariants ───────────────────────────────────────────────
//
// regime-1: zero-cost EBO collapse.  sizeof(JoinPolicy<Tier, T>) ==
// sizeof(T) at every supported tier.  Witnessed at three T sizes
// (1B, 4B, 8B) and across the full tier spectrum.
namespace detail::join_policy_layout {

template <typename T> using JoinAllJP       = JoinPolicy<JoinPolicy_v::JOIN_ALL,      T>;
template <typename T> using WaitDeadlineJP  = JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, T>;
template <typename T> using CancelJP        = JoinPolicy<JoinPolicy_v::CANCEL,        T>;
template <typename T> using AbandonJP       = JoinPolicy<JoinPolicy_v::ABANDON,       T>;
template <typename T> using DetachJP        = JoinPolicy<JoinPolicy_v::DETACH,        T>;
template <typename T> using ForgetJP        = JoinPolicy<JoinPolicy_v::FORGET,        T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllJP,      char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllJP,      int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(JoinAllJP,      double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WaitDeadlineJP, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CancelJP,       int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CancelJP,       double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbandonJP,      int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(DetachJP,       int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ForgetJP,       int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ForgetJP,       double);

}  // namespace detail::join_policy_layout

// Direct sizeof witnesses — EBO collapse must hold for the
// production-typical T sizes regardless of which tier is pinned.
static_assert(sizeof(JoinPolicy<JoinPolicy_v::FORGET,        int>)    == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::DETACH,        int>)    == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::ABANDON,       int>)    == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::CANCEL,        int>)    == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, int>)    == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::JOIN_ALL,      int>)    == sizeof(int));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::JOIN_ALL,      double>) == sizeof(double));
static_assert(sizeof(JoinPolicy<JoinPolicy_v::JOIN_ALL,      char>)   == sizeof(char));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::join_policy_self_test {

using JoinAllInt       = JoinPolicy<JoinPolicy_v::JOIN_ALL,      int>;
using WaitDeadlineInt  = JoinPolicy<JoinPolicy_v::WAIT_DEADLINE, int>;
using CancelInt        = JoinPolicy<JoinPolicy_v::CANCEL,        int>;
using AbandonInt       = JoinPolicy<JoinPolicy_v::ABANDON,       int>;
using DetachInt        = JoinPolicy<JoinPolicy_v::DETACH,        int>;
using ForgetInt        = JoinPolicy<JoinPolicy_v::FORGET,        int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr JoinAllInt jp_default{};
static_assert(jp_default.peek() == 0);
static_assert(jp_default.tier == JoinPolicy_v::JOIN_ALL);

inline constexpr JoinAllInt jp_explicit{42};
static_assert(jp_explicit.peek() == 42);

// ── Pinned tier accessor ───────────────────────────────────────────
static_assert(JoinAllInt::tier       == JoinPolicy_v::JOIN_ALL);
static_assert(WaitDeadlineInt::tier  == JoinPolicy_v::WAIT_DEADLINE);
static_assert(CancelInt::tier        == JoinPolicy_v::CANCEL);
static_assert(AbandonInt::tier       == JoinPolicy_v::ABANDON);
static_assert(DetachInt::tier        == JoinPolicy_v::DETACH);
static_assert(ForgetInt::tier        == JoinPolicy_v::FORGET);

// ── satisfies<RequiredTier> — subsumption-up direction ─────────────
//
// A JOIN_ALL producer satisfies every consumer.
static_assert(JoinAllInt::satisfies<JoinPolicy_v::JOIN_ALL>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::WAIT_DEADLINE>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::CANCEL>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::ABANDON>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::DETACH>);
static_assert(JoinAllInt::satisfies<JoinPolicy_v::FORGET>);

// A CANCEL producer satisfies weaker-or-equal consumers only.
static_assert( CancelInt::satisfies<JoinPolicy_v::CANCEL>);       // self
static_assert( CancelInt::satisfies<JoinPolicy_v::ABANDON>);      // weaker
static_assert( CancelInt::satisfies<JoinPolicy_v::DETACH>);
static_assert( CancelInt::satisfies<JoinPolicy_v::FORGET>);
static_assert(!CancelInt::satisfies<JoinPolicy_v::WAIT_DEADLINE>); // stricter
static_assert(!CancelInt::satisfies<JoinPolicy_v::JOIN_ALL>);

// A FORGET producer satisfies only FORGET consumers.
static_assert( ForgetInt::satisfies<JoinPolicy_v::FORGET>);
static_assert(!ForgetInt::satisfies<JoinPolicy_v::DETACH>);
static_assert(!ForgetInt::satisfies<JoinPolicy_v::CANCEL>);
static_assert(!ForgetInt::satisfies<JoinPolicy_v::JOIN_ALL>);

// ── relax<WeakerTier> — DOWN-the-lattice conversion ────────────────
//
// JOIN_ALL relaxes to any tier.
inline constexpr auto from_join_all_to_cancel =
    JoinAllInt{42}.relax<JoinPolicy_v::CANCEL>();
static_assert(from_join_all_to_cancel.peek() == 42);
static_assert(from_join_all_to_cancel.tier == JoinPolicy_v::CANCEL);

inline constexpr auto from_join_all_to_forget =
    JoinAllInt{99}.relax<JoinPolicy_v::FORGET>();
static_assert(from_join_all_to_forget.peek() == 99);
static_assert(from_join_all_to_forget.tier == JoinPolicy_v::FORGET);

// WAIT_DEADLINE relaxes to CANCEL.
inline constexpr auto from_wait_to_cancel =
    WaitDeadlineInt{7}.relax<JoinPolicy_v::CANCEL>();
static_assert(from_wait_to_cancel.peek() == 7);
static_assert(from_wait_to_cancel.tier == JoinPolicy_v::CANCEL);

// CANCEL relaxes to ABANDON.
inline constexpr auto from_cancel_to_abandon =
    CancelInt{55}.relax<JoinPolicy_v::ABANDON>();
static_assert(from_cancel_to_abandon.peek() == 55);
static_assert(from_cancel_to_abandon.tier == JoinPolicy_v::ABANDON);

// Reflexive: relax<SameTier> is a no-op.
inline constexpr auto identity_relax =
    JoinAllInt{100}.relax<JoinPolicy_v::JOIN_ALL>();
static_assert(identity_relax.peek() == 100);
static_assert(identity_relax.tier == JoinPolicy_v::JOIN_ALL);

// ── mint_join_policy §XXI factory ──────────────────────────────────
inline constexpr auto minted_join_all =
    mint_join_policy<JoinPolicy_v::JOIN_ALL, int>(123);
static_assert(minted_join_all.peek() == 123);
static_assert(minted_join_all.tier == JoinPolicy_v::JOIN_ALL);

// ── Convenience aliases round-trip ─────────────────────────────────
static_assert(std::is_same_v<join_policy_tier::JoinAll<int>,       JoinAllInt>);
static_assert(std::is_same_v<join_policy_tier::WaitDeadline<int>,  WaitDeadlineInt>);
static_assert(std::is_same_v<join_policy_tier::Cancel<int>,        CancelInt>);
static_assert(std::is_same_v<join_policy_tier::Abandon<int>,       AbandonInt>);
static_assert(std::is_same_v<join_policy_tier::Detach<int>,        DetachInt>);
static_assert(std::is_same_v<join_policy_tier::Forget<int>,        ForgetInt>);

// ── Equality at same tier ──────────────────────────────────────────
static_assert(JoinAllInt{42} == JoinAllInt{42});
static_assert(!(JoinAllInt{42} == JoinAllInt{43}));

// ── Copyability — JoinPolicy IS COPYABLE (unlike Secret) ──────────
static_assert(std::is_copy_constructible_v<JoinAllInt>);
static_assert(std::is_copy_assignable_v<JoinAllInt>);
static_assert(std::is_move_constructible_v<JoinAllInt>);
static_assert(std::is_move_assignable_v<JoinAllInt>);

// ── Modality declaration ───────────────────────────────────────────
static_assert(JoinAllInt::modality == ::crucible::algebra::ModalityKind::Comonad);

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Exercise construction / peek / extract / relax / mint_join_policy
// with non-constant arguments per the algebra/* runtime-smoke-test
// discipline (feedback_algebra_runtime_smoke_test_discipline).
// Critical because JoinPolicy routes through Graded::extract() (the
// Comonad counit); that path is distinct from peek() / consume()
// and a constexpr-vs-runtime divergence would silently misroute
// policy reads.
inline void runtime_smoke_test() {
    int seed = 17;                                          // non-constant

    JoinAllInt jp{seed * 2};
    if (jp.peek() != 34) std::abort();
    if (jp.tier != JoinPolicy_v::JOIN_ALL) std::abort();

    // peek_mut — Comonad+empty admits mutation.
    jp.peek_mut() = 99;
    if (jp.peek() != 99) std::abort();

    // extract — Comonad counit, unrestricted.
    JoinAllInt e{seed * 3};
    int extracted = std::move(e).extract();
    if (extracted != 51) std::abort();

    // relax — stricter → weaker.
    JoinAllInt source{seed * 4};
    auto relaxed = std::move(source).relax<JoinPolicy_v::CANCEL>();
    if (relaxed.peek() != 68) std::abort();
    if (relaxed.tier != JoinPolicy_v::CANCEL) std::abort();

    // Chain-relax through the entire spectrum.
    JoinAllInt full{seed};
    auto step1 = std::move(full).relax<JoinPolicy_v::WAIT_DEADLINE>();
    auto step2 = std::move(step1).relax<JoinPolicy_v::CANCEL>();
    auto step3 = std::move(step2).relax<JoinPolicy_v::ABANDON>();
    auto step4 = std::move(step3).relax<JoinPolicy_v::DETACH>();
    auto step5 = std::move(step4).relax<JoinPolicy_v::FORGET>();
    if (step5.peek() != 17) std::abort();
    if (step5.tier != JoinPolicy_v::FORGET) std::abort();

    // mint_join_policy factory path (in-place ctor).
    auto m = mint_join_policy<JoinPolicy_v::CANCEL, int>(seed);
    int m_out = std::move(m).extract();
    if (m_out != 17) std::abort();

    // Copy semantics — JoinPolicy IS COPYABLE (metadata, not classified).
    JoinAllInt c1{seed};
    JoinAllInt c2 = c1;                                     // copy ctor
    if (c1.peek() != c2.peek()) std::abort();
    if (c1.peek() != 17) std::abort();

    // swap — Comonad+empty admits swap.
    JoinAllInt a{1}, b{2};
    swap(a, b);
    if (a.peek() != 2 || b.peek() != 1) std::abort();
}

}  // namespace detail::join_policy_self_test

}  // namespace crucible::safety
