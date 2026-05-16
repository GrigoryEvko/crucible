#pragma once

// ── crucible::safety::witness — Aggregation.h (Followup D) ────────────
//
// Multi-platform witness aggregation.  PlatformBounded<W, P> claims
// evidence W for platform P; today's single binding can only carry one
// PlatformBounded slot.  Followup D ships `MultiplatformWitness<...>`,
// a variadic carrier admitting a pack of witnesses (each typically a
// PlatformBounded<W, Platform> or a bare W).  A binary compiled for one
// platform consults its pack: if ANY witness in the pack is valid for
// the current platform AND satisfies the floor demand, the
// MultiplatformWitness satisfies the floor.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   template <typename... Witnesses>
//   struct MultiplatformWitness    — variadic union carrier.
//
//   template <typename W, typename Platform>
//   inline constexpr bool claims_on_platform_v
//                                  — true iff W carries evidence valid
//                                    on Platform.
//
//   intersect_witnesses_t<W1, W2, ...>
//                                  — meet across witnesses (lowest
//                                    tier present on every witness).
//
//   union_witnesses_t<W1, W2, ...>
//                                  — join (highest tier present in
//                                    the pack).
//
// ── Aggregation semantics ───────────────────────────────────────────
//
//   WitnessAtLeast<MultiplatformWitness<...>, Min> is satisfied iff
//   ANY witness in the pack carrying evidence ON THE CURRENT PLATFORM
//   meets Min in the lattice.  An aggregator with PlatformBounded<
//   Tested<id>, X86_64> + PlatformBounded<Tested<id2>, AArch64> meets
//   the Tested floor on either x86 OR aarch64 fleets.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — concepts gate the dispatch; no implicit conversion.
//   InitSafe — variadic carrier is an empty struct (sizeof == 1).
//   DetSafe  — bit-identical across compiles for a given pack.
//
// ── References ──────────────────────────────────────────────────────
//
//   safety/witness/Witness.h    — PlatformBounded primary template
//   safety/witness/IsWitness.h  — IsWitness / WitnessAtLeast concept
//   safety/witness/Platform.h   — arch phantom tags

#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Platform.h>
#include <crucible/safety/witness/Witness.h>

#include <type_traits>

namespace crucible::safety::witness {

// ═════════════════════════════════════════════════════════════════════
// ── MultiplatformWitness — variadic union carrier ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Empty struct; sizeof == 1.  The pack is purely type-level metadata.

template <typename... Witnesses>
struct MultiplatformWitness final {};

static_assert(sizeof(MultiplatformWitness<>) == 1);

// ── claims_on_platform_v ────────────────────────────────────────────
//
// True iff W carries evidence valid on Platform.  Three cases:
//   * Bare witness W (Asserted/Tested/CrossValidated/FormallyVerified):
//     valid on EVERY platform (no platform restriction).
//   * PlatformBounded<W', Platforms...>: valid iff Platform is in
//     Platforms...
//   * MultiplatformWitness<W1, ...>: valid iff any Wi is valid on Platform.

namespace detail {

template <typename W, typename Platform>
inline constexpr bool claims_on_platform_v_impl = true;

template <typename R, typename Platform>
inline constexpr bool claims_on_platform_v_impl<Asserted<R>, Platform> = true;

template <auto Id, typename Platform>
inline constexpr bool claims_on_platform_v_impl<Tested<Id>, Platform> = true;

template <auto Id, typename Platform>
inline constexpr bool claims_on_platform_v_impl<CrossValidated<Id>, Platform> = true;

template <typename P, typename Platform>
inline constexpr bool claims_on_platform_v_impl<FormallyVerified<P>, Platform> = true;

template <typename W, typename... Platforms, typename Platform>
inline constexpr bool claims_on_platform_v_impl<PlatformBounded<W, Platforms...>, Platform> =
    (std::is_same_v<Platforms, Platform> || ...);

template <typename Platform, typename... Ws>
inline constexpr bool claims_on_platform_v_impl<MultiplatformWitness<Ws...>, Platform> =
    (claims_on_platform_v_impl<Ws, Platform> || ... || false);

}  // namespace detail

template <typename W, typename Platform>
inline constexpr bool claims_on_platform_v =
    detail::claims_on_platform_v_impl<std::remove_cvref_t<W>, Platform>;

// ── MultiplatformWitness participates in IsWitness ─────────────────
//
// Specialize the canonical-witness recognizer so MultiplatformWitness
// pack members satisfy the IsWitness concept.

namespace detail {

template <typename... Ws>
inline constexpr bool is_canonical_witness_v<MultiplatformWitness<Ws...>> =
    (is_canonical_witness_v<std::remove_cvref_t<Ws>> && ... && true);

// ── MultiplatformWitness tier resolution ───────────────────────────
//
// The aggregator's tier on the current platform is the MAXIMUM tier
// of any witness in the pack that claims evidence on the current
// platform.  If no witness claims, fall back to the Asserted floor.

template <typename Platform, typename... Ws>
inline constexpr std::uint8_t multiplatform_tier_for_v = 0;

template <typename Platform, typename W>
inline constexpr std::uint8_t multiplatform_tier_for_v<Platform, W> =
    claims_on_platform_v_impl<W, Platform> ? witness_tier_v_impl<W> : std::uint8_t{0};

template <typename Platform, typename W, typename... Rest>
inline constexpr std::uint8_t multiplatform_tier_for_v<Platform, W, Rest...> =
    multiplatform_tier_for_v<Platform, W> >= multiplatform_tier_for_v<Platform, Rest...>
        ? multiplatform_tier_for_v<Platform, W>
        : multiplatform_tier_for_v<Platform, Rest...>;

template <typename... Ws>
inline constexpr std::uint8_t witness_tier_v_impl<MultiplatformWitness<Ws...>> = []() {
    constexpr auto highest =
        multiplatform_tier_for_v<arch::current_arch_tag, Ws...>;
    return (highest > 0) ? highest : std::uint8_t{1};  // floor to Asserted
}();

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── platform_set_of<W> — type-pack of claimed platforms ────────────
// ═════════════════════════════════════════════════════════════════════
//
// Returns a type pack listing every platform W carries evidence for.
// Used by federation tooling that wants to display the per-binding
// platform-coverage matrix.

template <typename... Platforms>
struct PlatformPack final {};

namespace detail {

template <typename W>
struct platform_set_of_impl {
    using type = PlatformPack<>;  // bare witness — no platform pin
};

template <typename W, typename... Platforms>
struct platform_set_of_impl<PlatformBounded<W, Platforms...>> {
    using type = PlatformPack<Platforms...>;
};

template <typename... Ws>
struct multiplatform_collect;

template <>
struct multiplatform_collect<> {
    using type = PlatformPack<>;
};

template <typename... Ps1, typename... Ps2>
constexpr auto pack_concat(PlatformPack<Ps1...>, PlatformPack<Ps2...>)
    -> PlatformPack<Ps1..., Ps2...>;

template <typename W, typename... Rest>
struct multiplatform_collect<W, Rest...> {
    using head_pack = typename platform_set_of_impl<W>::type;
    using rest_pack = typename multiplatform_collect<Rest...>::type;
    using type = decltype(pack_concat(head_pack{}, rest_pack{}));
};

template <typename... Ws>
struct platform_set_of_impl<MultiplatformWitness<Ws...>> {
    using type = typename multiplatform_collect<Ws...>::type;
};

}  // namespace detail

template <typename W>
using platform_set_of = typename detail::platform_set_of_impl<
    std::remove_cvref_t<W>>::type;

// ═════════════════════════════════════════════════════════════════════
// ── intersect_witnesses_t / union_witnesses_t ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Lattice meet/join over a fixed pack of witnesses, by tier.

namespace detail {

template <typename Lhs, typename Rhs>
using meet_witness_t =
    std::conditional_t<witness_tier_v_impl<Lhs> <= witness_tier_v_impl<Rhs>, Lhs, Rhs>;

template <typename Lhs, typename Rhs>
using join_witness_t =
    std::conditional_t<witness_tier_v_impl<Lhs> >= witness_tier_v_impl<Rhs>, Lhs, Rhs>;

template <typename... Ws>
struct intersect_witnesses_impl;

template <typename W>
struct intersect_witnesses_impl<W> { using type = W; };

template <typename W1, typename W2, typename... Rest>
struct intersect_witnesses_impl<W1, W2, Rest...> {
    using step = meet_witness_t<W1, W2>;
    using type = typename intersect_witnesses_impl<step, Rest...>::type;
};

template <typename... Ws>
struct union_witnesses_impl;

template <typename W>
struct union_witnesses_impl<W> { using type = W; };

template <typename W1, typename W2, typename... Rest>
struct union_witnesses_impl<W1, W2, Rest...> {
    using step = join_witness_t<W1, W2>;
    using type = typename union_witnesses_impl<step, Rest...>::type;
};

}  // namespace detail

template <typename... Ws>
using intersect_witnesses_t =
    typename detail::intersect_witnesses_impl<Ws...>::type;

template <typename... Ws>
using union_witnesses_t =
    typename detail::union_witnesses_impl<Ws...>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace aggregation_self_test {

// Bare witness claims on every platform.
static_assert(claims_on_platform_v<Asserted<>, arch::X86_64>);
static_assert(claims_on_platform_v<Asserted<>, arch::AArch64>);
static_assert(claims_on_platform_v<Tested<7>, arch::X86_64>);

// PlatformBounded claims only on its pinned platforms.
static_assert(claims_on_platform_v<PlatformBounded<Tested<0>, arch::X86_64>, arch::X86_64>);
static_assert(!claims_on_platform_v<PlatformBounded<Tested<0>, arch::X86_64>, arch::AArch64>);

// MultiplatformWitness claims on any platform any sub-witness claims.
using MP = MultiplatformWitness<
    PlatformBounded<Asserted<>, arch::X86_64>,
    PlatformBounded<Tested<7>, arch::AArch64>>;

static_assert(IsWitness<MP>);
static_assert(claims_on_platform_v<MP, arch::X86_64>);
static_assert(claims_on_platform_v<MP, arch::AArch64>);
static_assert(!claims_on_platform_v<MP, arch::RISCV>);

// Tier reduction.  On the current platform, MP reports the max tier
// of any claiming witness.  On X86_64, only the Asserted PB claims —
// tier 1.  On AArch64, the Tested PB claims — tier 2.  On RISCV,
// neither claims — floor to Asserted (tier 1).
//
// (We can't statically pick "the platform we're on" without making
// the test platform-conditional, so we check the IsWitness shape +
// claims_on_platform_v above.  Tier ladder is exercised in the neg
// fixture and downstream specializations.)

// intersect / union round-trip.
static_assert(std::is_same_v<intersect_witnesses_t<Asserted<>, Tested<0>>, Asserted<>>);
static_assert(std::is_same_v<union_witnesses_t<Asserted<>, Tested<0>>, Tested<0>>);
static_assert(std::is_same_v<intersect_witnesses_t<Tested<0>, CrossValidated<0>>, Tested<0>>);
static_assert(std::is_same_v<union_witnesses_t<Tested<0>, CrossValidated<0>>, CrossValidated<0>>);

}  // namespace aggregation_self_test

}  // namespace crucible::safety::witness
