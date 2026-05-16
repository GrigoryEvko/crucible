#pragma once

// ── crucible::fixy — Witness.h (FIXY-G9) ───────────────────────────────
//
// Consumer-side gate for per-axis witness floor demand on a fixy::fn.
// Downstream code that requires a minimum witness tier on a specific
// dim writes:
//
//   template <typename F>
//       requires fixy::FnWitnessAtLeast<F, fixy::dim::Trust,
//                                       safety::witness::Tested<id>>
//   void promote_to_hot_cipher(F&&);
//
// The concept finds the grant in F's pack that engages the named dim,
// reads its `witness_t`, and demands `WitnessAtLeast<witness_t, MinW>`.
// A bare grant (witness = Asserted) is rejected; an evidenced
// `cg::*_e<Tested<id>>` variant is admitted.
//
// ── Surface ──────────────────────────────────────────────────────────
//
//   fixy::axis_witness_t<F, Axis>           — witness type for F's
//                                              engaging grant on Axis.
//   fixy::FnWitnessAtLeast<F, Axis, MinW>   — concept gate.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — concept-driven dispatch; no implicit conversion.
//   DetSafe  — bit-identical witness type extraction.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §6 Phase G    — G9 witness metadata
//   safety/witness/Witness.h              — four-tier witness lattice
//   safety/witness/IsWitness.h            — WitnessAtLeast concept
//   fixy/Reflect.h                        — IsFixyFn concept

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Reflect.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

#include <type_traits>

namespace crucible::fixy {

namespace detail {

// Pull the witness_t of the FIRST grant in Grants... engaging Axis.
// Empty pack or no-match falls back to DefaultWitness (Asserted floor).
template <dim::DimAxis Axis, typename... Grants>
struct axis_witness_impl {
    using type = ::crucible::safety::witness::DefaultWitness;
};

template <dim::DimAxis Axis, typename G, typename... Rest>
struct axis_witness_impl<Axis, G, Rest...> {
    using type = std::conditional_t<
        engages_dim_v<G, Axis>,
        typename G::witness_t,
        typename axis_witness_impl<Axis, Rest...>::type
    >;
};

template <typename F, dim::DimAxis Axis>
struct axis_witness_for_fn;

template <typename T, typename... Grants, dim::DimAxis Axis>
struct axis_witness_for_fn<::crucible::fixy::fn<T, Grants...>, Axis> {
    using type = typename axis_witness_impl<Axis, Grants...>::type;
};

}  // namespace detail

template <typename F, dim::DimAxis Axis>
    requires IsFixyFn<F>
using axis_witness_t =
    typename detail::axis_witness_for_fn<std::remove_cvref_t<F>, Axis>::type;

// FnWitnessAtLeast<F, Axis, MinW>: F's grant on Axis carries a witness
// at least as strong as MinW in the four-tier lattice.

template <typename F, dim::DimAxis Axis, typename MinW>
concept FnWitnessAtLeast =
    IsFixyFn<F> &&
    ::crucible::safety::witness::IsWitness<MinW> &&
    ::crucible::safety::witness::WitnessAtLeast<axis_witness_t<F, Axis>, MinW>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-tests ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace witness_self_test {

namespace sw = ::crucible::safety::witness;

using AllStrictBareFn = fn<int,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    accept_default_strict_for<dim::Reentrancy>,
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

// Every axis on a bare binding reports DefaultWitness (Asserted).
static_assert(std::is_same_v<axis_witness_t<AllStrictBareFn, dim::Trust>,
                             sw::DefaultWitness>);
static_assert(std::is_same_v<axis_witness_t<AllStrictBareFn, dim::Reentrancy>,
                             sw::DefaultWitness>);

// Asserted floor passes.
static_assert(FnWitnessAtLeast<AllStrictBareFn, dim::Trust, sw::Asserted<>>);
static_assert(FnWitnessAtLeast<AllStrictBareFn, dim::Reentrancy, sw::Asserted<>>);

// Tested floor fails on Asserted binding.
static_assert(!FnWitnessAtLeast<AllStrictBareFn, dim::Trust, sw::Tested<0>>);
static_assert(!FnWitnessAtLeast<AllStrictBareFn, dim::Reentrancy, sw::Tested<0>>);

// Evidenced binding lifts the floor.
using EvidencedReentrant = fn<int,
    accept_default_strict_for<dim::Type>,
    accept_default_strict_for<dim::Refinement>,
    accept_default_strict_for<dim::Usage>,
    accept_default_strict_for<dim::Effect>,
    accept_default_strict_for<dim::Security>,
    accept_default_strict_for<dim::Protocol>,
    accept_default_strict_for<dim::Lifetime>,
    accept_default_strict_for<dim::Provenance>,
    accept_default_strict_for<dim::Trust>,
    accept_default_strict_for<dim::Representation>,
    accept_default_strict_for<dim::Observability>,
    accept_default_strict_for<dim::Complexity>,
    accept_default_strict_for<dim::Precision>,
    accept_default_strict_for<dim::Space>,
    accept_default_strict_for<dim::Overflow>,
    accept_default_strict_for<dim::Mutation>,
    grant::reentrant_e<sw::Tested<99>>,                  // Reentrancy + Tested
    accept_default_strict_for<dim::Size>,
    accept_default_strict_for<dim::Version>,
    accept_default_strict_for<dim::Staleness>
>;

static_assert(std::is_same_v<axis_witness_t<EvidencedReentrant, dim::Reentrancy>,
                             sw::Tested<99>>);

static_assert(FnWitnessAtLeast<EvidencedReentrant, dim::Reentrancy, sw::Tested<99>>);
static_assert(FnWitnessAtLeast<EvidencedReentrant, dim::Reentrancy, sw::Asserted<>>);
static_assert(!FnWitnessAtLeast<EvidencedReentrant, dim::Reentrancy,
                                sw::CrossValidated<0>>);

}  // namespace witness_self_test

}  // namespace crucible::fixy
