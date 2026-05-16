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

}  // namespace self_test_concept

}  // namespace crucible::safety::witness
