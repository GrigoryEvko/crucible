#pragma once

// ── crucible::safety::witness — IsWitness / WitnessAtLeast (FIXY-G9) ──
//
// Concept gates for the four-tier witness hierarchy.  Consumer-side
// `requires WitnessAtLeast<W, Min>` enforces a minimum proof-relevance
// tier.  `IsWitness<W>` is the universal recognizer — every type
// admitted into the witness slot satisfies it.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   IsWitness<W>          — true iff W is one of the four canonical
//                            witness types or a PlatformBounded<W',
//                            Platforms...> where W' satisfies IsWitness.
//
//   WitnessAtLeast<W, Min> — true iff IsWitness<W> AND
//                            witness_leq_v<Min, W> (W is at least as
//                            strong as Min in the lattice).
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — concepts are pure value-domain predicates.
//   DetSafe  — bit-identical across compiles.
//
// ── References ──────────────────────────────────────────────────────
//
//   safety/witness/Witness.h — witness type definitions + lattice
//   fixy/Witness.h            — fixy-side FnWitnessAtLeast<F, Axis, Min>

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
inline constexpr bool is_canonical_witness_v<PlatformBounded<W, Platforms...>> =
    is_canonical_witness_v<W>;

}  // namespace detail

template <typename W>
concept IsWitness = detail::is_canonical_witness_v<std::remove_cvref_t<W>>;

template <typename W, typename Min>
concept WitnessAtLeast =
    IsWitness<W> && IsWitness<Min> && witness_leq_v<Min, W>;

// ═════════════════════════════════════════════════════════════════════
// ── is_valid_witness_v (Followup C) ────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per-witness validity check that consults the per-type registry:
//
//   * Asserted<R>            — always valid (no registry; the binding
//                              IS the assertion).
//   * Tested<TestId>         — valid iff
//                              ::crucible::safety::diag::is_active_test_v<TestId>.
//                              A Revoked/Stale/Expired entry returns false.
//   * CrossValidated<CiRunId> — valid iff
//                              ::crucible::safety::diag::is_valid_ci_run_v<CiRunId>.
//   * FormallyVerified<P>    — always valid (no registry; the proof
//                              cert IS the validity).
//   * PlatformBounded<W, ...> — validity of the underlying W only when
//                              the current platform is in Platforms...;
//                              otherwise valid as Asserted floor.
//
// Consumers that demand a Tested-or-stronger floor AND validity write:
//
//   requires WitnessAtLeast<W, Tested<...>> && is_valid_witness_v<W>

namespace detail {

template <typename W>
inline constexpr bool is_valid_witness_v_impl = true;

template <typename R>
inline constexpr bool is_valid_witness_v_impl<Asserted<R>> = true;

template <auto Id>
inline constexpr bool is_valid_witness_v_impl<Tested<Id>> =
    ::crucible::safety::diag::is_active_test_v<Id>;

template <auto Id>
inline constexpr bool is_valid_witness_v_impl<CrossValidated<Id>> =
    ::crucible::safety::diag::is_valid_ci_run_v<Id>;

template <typename P>
inline constexpr bool is_valid_witness_v_impl<FormallyVerified<P>> = true;

template <typename W, typename... Platforms>
inline constexpr bool is_valid_witness_v_impl<PlatformBounded<W, Platforms...>> =
    platform_bounded_active_v<Platforms...>
        ? is_valid_witness_v_impl<W>
        : true;  // inactive on current platform => degrades to Asserted floor

}  // namespace detail

template <typename W>
inline constexpr bool is_valid_witness_v =
    detail::is_valid_witness_v_impl<std::remove_cvref_t<W>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace self_test_concept {

static_assert(IsWitness<Asserted<>>);
static_assert(IsWitness<Asserted<UnnamedRationale>>);
static_assert(IsWitness<Tested<0>>);
static_assert(IsWitness<CrossValidated<99>>);
static_assert(IsWitness<FormallyVerified<UnnamedRationale>>);
static_assert(IsWitness<PlatformBounded<Tested<0>, arch::X86_64>>);

static_assert(!IsWitness<int>);
static_assert(!IsWitness<void>);
static_assert(!IsWitness<UnnamedRationale>);  // rationale is NOT a witness
static_assert(!IsWitness<arch::X86_64>);     // arch is NOT a witness

// WitnessAtLeast — same-tier passes.
static_assert(WitnessAtLeast<Asserted<>, Asserted<>>);
static_assert(WitnessAtLeast<Tested<0>, Tested<0>>);
static_assert(WitnessAtLeast<CrossValidated<0>, CrossValidated<0>>);

// Higher passes against lower floor.
static_assert(WitnessAtLeast<Tested<0>, Asserted<>>);
static_assert(WitnessAtLeast<CrossValidated<0>, Tested<0>>);
static_assert(WitnessAtLeast<FormallyVerified<int>, Asserted<>>);

// Lower fails against higher floor.
static_assert(!WitnessAtLeast<Asserted<>, Tested<0>>);
static_assert(!WitnessAtLeast<Tested<0>, CrossValidated<0>>);
static_assert(!WitnessAtLeast<CrossValidated<0>, FormallyVerified<int>>);

// Followup C — is_valid_witness_v.
static_assert(is_valid_witness_v<Asserted<>>);
static_assert(is_valid_witness_v<Asserted<UnnamedRationale>>);
static_assert(is_valid_witness_v<Tested<0>>);                     // primary template = Active
static_assert(is_valid_witness_v<Tested<::crucible::safety::diag::id::fixy_custom_optimizer>>);
static_assert(!is_valid_witness_v<Tested<::crucible::safety::diag::id::fixy_revoked_demo>>);
static_assert(is_valid_witness_v<CrossValidated<::crucible::safety::diag::ci_id::fixy_cross_vendor_smoke>>);
static_assert(!is_valid_witness_v<CrossValidated<::crucible::safety::diag::ci_id::fixy_revoked_ci_demo>>);
static_assert(is_valid_witness_v<FormallyVerified<UnnamedRationale>>);

}  // namespace self_test_concept

}  // namespace crucible::safety::witness
