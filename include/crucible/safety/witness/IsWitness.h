#pragma once

#include <crucible/safety/diag/CiRunRegistry.h>
#include <crucible/safety/diag/TestRegistry.h>
#include <crucible/safety/witness/Witness.h>

#include <type_traits>

namespace crucible::safety::witness {

namespace detail {

template <typename>
inline constexpr bool is_canonical_witness_v = false;

template <typename R>
inline constexpr bool is_canonical_witness_v<Asserted<R>> = true;

template <auto Id>
inline constexpr bool is_canonical_witness_v<Tested<Id>> = true;

template <auto Id>
inline constexpr bool is_canonical_witness_v<CrossValidated<Id>> = true;

template <typename P>
inline constexpr bool is_canonical_witness_v<FormallyVerified<P>> = true;

template <typename W, typename... Platforms>
inline constexpr bool is_canonical_witness_v<PlatformBounded<W, Platforms...>> = is_canonical_witness_v<W>;

}  // namespace detail

template <typename W>
concept IsWitness = detail::is_canonical_witness_v<std::remove_cvref_t<W>>;

template <typename W, typename Min>
concept WitnessAtLeast = IsWitness<W> && IsWitness<Min> && witness_leq_v<Min, W>;

namespace detail {

template <typename W>
inline constexpr bool is_valid_witness_v_impl = true;

template <typename R>
inline constexpr bool is_valid_witness_v_impl<Asserted<R>> = true;

template <auto Id>
inline constexpr bool is_valid_witness_v_impl<Tested<Id>> = ::crucible::safety::diag::is_active_test_v<Id>;

template <auto Id>
inline constexpr bool is_valid_witness_v_impl<CrossValidated<Id>> = ::crucible::safety::diag::is_valid_ci_run_v<Id>;

template <typename P>
inline constexpr bool is_valid_witness_v_impl<FormallyVerified<P>> = true;

// A bound that names no platform in force here makes no claim about this
// build, so it holds at the floor tier rather than reading as a failed proof.
template <typename W, typename... Platforms>
inline constexpr bool is_valid_witness_v_impl<PlatformBounded<W, Platforms...>> =
    platform_bounded_active_v<Platforms...> ? is_valid_witness_v_impl<W> : true;

}  // namespace detail

template <typename W>
inline constexpr bool is_valid_witness_v = detail::is_valid_witness_v_impl<std::remove_cvref_t<W>>;

namespace self_test_concept {

static_assert(IsWitness<Asserted<>>);
static_assert(IsWitness<Asserted<UnnamedRationale>>);
static_assert(IsWitness<Tested<0>>);
static_assert(IsWitness<CrossValidated<99>>);
static_assert(IsWitness<FormallyVerified<UnnamedRationale>>);
static_assert(IsWitness<PlatformBounded<Tested<0>, arch::X86_64>>);

static_assert(!IsWitness<int>);
static_assert(!IsWitness<void>);
static_assert(!IsWitness<UnnamedRationale>);
static_assert(!IsWitness<arch::X86_64>);

static_assert(WitnessAtLeast<Asserted<>, Asserted<>>);
static_assert(WitnessAtLeast<Tested<0>, Tested<0>>);
static_assert(WitnessAtLeast<CrossValidated<0>, CrossValidated<0>>);

static_assert(WitnessAtLeast<Tested<0>, Asserted<>>);
static_assert(WitnessAtLeast<CrossValidated<0>, Tested<0>>);
static_assert(WitnessAtLeast<FormallyVerified<int>, Asserted<>>);

static_assert(!WitnessAtLeast<Asserted<>, Tested<0>>);
static_assert(!WitnessAtLeast<Tested<0>, CrossValidated<0>>);
static_assert(!WitnessAtLeast<CrossValidated<0>, FormallyVerified<int>>);

static_assert(is_valid_witness_v<Asserted<>>);
static_assert(is_valid_witness_v<Asserted<UnnamedRationale>>);
static_assert(is_valid_witness_v<Tested<0>>);
static_assert(is_valid_witness_v<Tested<::crucible::safety::diag::id::fixy_custom_optimizer>>);
static_assert(!is_valid_witness_v<Tested<::crucible::safety::diag::id::fixy_revoked_demo>>);
static_assert(is_valid_witness_v<CrossValidated<::crucible::safety::diag::ci_id::fixy_cross_vendor_smoke>>);
static_assert(!is_valid_witness_v<CrossValidated<::crucible::safety::diag::ci_id::fixy_revoked_ci_demo>>);
static_assert(is_valid_witness_v<FormallyVerified<UnnamedRationale>>);

}  // namespace self_test_concept

}  // namespace crucible::safety::witness
