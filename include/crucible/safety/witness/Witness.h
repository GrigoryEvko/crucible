#pragma once

#include <crucible/Platform.h>
#include <crucible/safety/witness/Platform.h>

#include <cstdint>
#include <type_traits>

namespace crucible::safety::witness {

struct UnnamedRationale final {};

template <typename Rationale = UnnamedRationale>
struct Asserted final {
    using rationale_type = Rationale;
};

template <auto TestId>
struct Tested final {
    static constexpr auto test_id_v = TestId;
};

template <auto CiRunId>
struct CrossValidated final {
    static constexpr auto ci_run_id_v = CiRunId;
};

template <typename ProofCert>
struct FormallyVerified final {
    using proof_cert_type = ProofCert;
};

static_assert(sizeof(Asserted<>) == 1);
static_assert(sizeof(Tested<0>) == 1);
static_assert(sizeof(CrossValidated<0>) == 1);
static_assert(sizeof(FormallyVerified<UnnamedRationale>) == 1);

using DefaultWitness = Asserted<UnnamedRationale>;

namespace detail {

template <typename W>
inline constexpr std::uint8_t witness_tier_v_impl = 0;

template <typename R>
inline constexpr std::uint8_t witness_tier_v_impl<Asserted<R>> = 1;

template <auto Id>
inline constexpr std::uint8_t witness_tier_v_impl<Tested<Id>> = 2;

template <auto Id>
inline constexpr std::uint8_t witness_tier_v_impl<CrossValidated<Id>> = 3;

template <typename P>
inline constexpr std::uint8_t witness_tier_v_impl<FormallyVerified<P>> = 4;

}  // namespace detail

template <typename W, typename... Platforms>
struct PlatformBounded final {
    using base_witness_type = W;
};

namespace detail {

template <typename... Platforms>
inline constexpr bool platform_bounded_active_v = (std::is_same_v<arch::current_arch_tag, Platforms> || ...);

// An inactive bound reports the floor tier, not the tier of the witness it
// wraps. That is what makes a bound witness fail a Tested-or-higher gate on an
// architecture the bound does not name.
template <typename W, typename... Platforms>
inline constexpr std::uint8_t witness_tier_v_impl<PlatformBounded<W, Platforms...>> =
    platform_bounded_active_v<Platforms...> ? witness_tier_v_impl<W> : std::uint8_t{1};

}  // namespace detail

template <typename W>
inline constexpr std::uint8_t witness_tier_v = detail::witness_tier_v_impl<W>;

template <typename W1, typename W2>
inline constexpr bool witness_leq_v = witness_tier_v<W1> <= witness_tier_v<W2>;

namespace self_test {

struct test_rationale {};
struct test_ci_run {};
struct test_proof_cert {};

static_assert(witness_tier_v<Asserted<UnnamedRationale>> == 1);
static_assert(witness_tier_v<Asserted<test_rationale>> == 1);
static_assert(witness_tier_v<Tested<42>> == 2);
static_assert(witness_tier_v<CrossValidated<7>> == 3);
static_assert(witness_tier_v<FormallyVerified<test_proof_cert>> == 4);

static_assert(witness_leq_v<Asserted<>, Asserted<>>);
static_assert(witness_leq_v<Tested<0>, Tested<0>>);
static_assert(witness_leq_v<CrossValidated<0>, CrossValidated<0>>);
static_assert(witness_leq_v<FormallyVerified<int>, FormallyVerified<int>>);

static_assert(witness_leq_v<Asserted<>, Tested<0>>);
static_assert(witness_leq_v<Tested<0>, CrossValidated<0>>);
static_assert(witness_leq_v<CrossValidated<0>, FormallyVerified<int>>);
static_assert(witness_leq_v<Asserted<>, FormallyVerified<int>>);

static_assert(!witness_leq_v<Tested<0>, Asserted<>>);
static_assert(!witness_leq_v<CrossValidated<0>, Tested<0>>);
static_assert(!witness_leq_v<FormallyVerified<int>, CrossValidated<0>>);
static_assert(!witness_leq_v<FormallyVerified<int>, Asserted<>>);

using AnyArch = arch::current_arch_tag;
static_assert(witness_tier_v<PlatformBounded<Tested<0>, AnyArch>> == 2,
              "PlatformBounded with current arch in pack must report W's tier.");

namespace pb_inactive_arch {
#if defined(__x86_64__)
using other = arch::AArch64;
#elif defined(__aarch64__)
using other = arch::X86_64;
#elif defined(__riscv)
using other = arch::X86_64;
#endif
}  // namespace pb_inactive_arch
static_assert(witness_tier_v<PlatformBounded<Tested<0>, pb_inactive_arch::other>> == 1,
              "PlatformBounded with current arch NOT in pack must fall back to "
              "Asserted floor (tier 1).");

static_assert(std::is_same_v<DefaultWitness, Asserted<UnnamedRationale>>);
static_assert(witness_tier_v<DefaultWitness> == 1);

}  // namespace self_test

}  // namespace crucible::safety::witness
