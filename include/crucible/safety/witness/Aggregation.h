#pragma once

#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Platform.h>
#include <crucible/safety/witness/Witness.h>

#include <type_traits>

namespace crucible::safety::witness {

template <typename... Witnesses>
struct MultiplatformWitness final {};

static_assert(sizeof(MultiplatformWitness<>) == 1);

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
inline constexpr bool claims_on_platform_v = detail::claims_on_platform_v_impl<std::remove_cvref_t<W>, Platform>;

namespace detail {

template <typename... Ws>
inline constexpr bool is_canonical_witness_v<MultiplatformWitness<Ws...>> =
    (is_canonical_witness_v<std::remove_cvref_t<Ws>> && ... && true);

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
    constexpr auto highest = multiplatform_tier_for_v<arch::current_arch_tag, Ws...>;
    // A pack whose members all stay silent on this architecture still reports
    // the floor tier, not the zero that marks a non-witness type.
    return (highest > 0) ? highest : std::uint8_t{1};
}();

}  // namespace detail

template <typename... Platforms>
struct PlatformPack final {};

namespace detail {

template <typename W>
struct platform_set_of_impl {
    // An empty pack means unpinned, so valid everywhere. It does not mean the
    // witness covers no platform.
    using type = PlatformPack<>;
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
constexpr auto pack_concat(PlatformPack<Ps1...>, PlatformPack<Ps2...>) -> PlatformPack<Ps1..., Ps2...>;

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
using platform_set_of = typename detail::platform_set_of_impl<std::remove_cvref_t<W>>::type;

namespace detail {

template <typename Lhs, typename Rhs>
using meet_witness_t = std::conditional_t<witness_tier_v_impl<Lhs> <= witness_tier_v_impl<Rhs>, Lhs, Rhs>;

template <typename Lhs, typename Rhs>
using join_witness_t = std::conditional_t<witness_tier_v_impl<Lhs> >= witness_tier_v_impl<Rhs>, Lhs, Rhs>;

template <typename... Ws>
struct intersect_witnesses_impl;

template <typename W>
struct intersect_witnesses_impl<W> {
    using type = W;
};

template <typename W1, typename W2, typename... Rest>
struct intersect_witnesses_impl<W1, W2, Rest...> {
    using step = meet_witness_t<W1, W2>;
    using type = typename intersect_witnesses_impl<step, Rest...>::type;
};

template <typename... Ws>
struct union_witnesses_impl;

template <typename W>
struct union_witnesses_impl<W> {
    using type = W;
};

template <typename W1, typename W2, typename... Rest>
struct union_witnesses_impl<W1, W2, Rest...> {
    using step = join_witness_t<W1, W2>;
    using type = typename union_witnesses_impl<step, Rest...>::type;
};

}  // namespace detail

template <typename... Ws>
using intersect_witnesses_t = typename detail::intersect_witnesses_impl<Ws...>::type;

template <typename... Ws>
using union_witnesses_t = typename detail::union_witnesses_impl<Ws...>::type;

namespace aggregation_self_test {

static_assert(claims_on_platform_v<Asserted<>, arch::X86_64>);
static_assert(claims_on_platform_v<Asserted<>, arch::AArch64>);
static_assert(claims_on_platform_v<Tested<7>, arch::X86_64>);

static_assert(claims_on_platform_v<PlatformBounded<Tested<0>, arch::X86_64>, arch::X86_64>);
static_assert(!claims_on_platform_v<PlatformBounded<Tested<0>, arch::X86_64>, arch::AArch64>);

using MP = MultiplatformWitness<PlatformBounded<Asserted<>, arch::X86_64>, PlatformBounded<Tested<7>, arch::AArch64>>;

static_assert(IsWitness<MP>);
static_assert(claims_on_platform_v<MP, arch::X86_64>);
static_assert(claims_on_platform_v<MP, arch::AArch64>);
static_assert(!claims_on_platform_v<MP, arch::RISCV>);

// The tier a pack reports depends on the architecture being built for, so
// asserting the ladder here would make this block platform-conditional. The
// shape checks above stand in for it.

static_assert(std::is_same_v<intersect_witnesses_t<Asserted<>, Tested<0>>, Asserted<>>);
static_assert(std::is_same_v<union_witnesses_t<Asserted<>, Tested<0>>, Tested<0>>);
static_assert(std::is_same_v<intersect_witnesses_t<Tested<0>, CrossValidated<0>>, Tested<0>>);
static_assert(std::is_same_v<union_witnesses_t<Tested<0>, CrossValidated<0>>, CrossValidated<0>>);

}  // namespace aggregation_self_test

}  // namespace crucible::safety::witness
