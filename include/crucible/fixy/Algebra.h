#pragma once

// ── crucible::fixy::algebra — Graded substrate + 30 lattices ───────
//
// Phase D re-export per misc/16_05_2026_fixy.md.  Surfaces the full
// Graded<Modality, Lattice, T> algebraic substrate plus every
// concrete lattice from algebra/lattices/ under `fixy::algebra::` so
// callers who include only the fixy umbrella never have to descend
// into the algebra/ tree to instantiate a graded wrapper.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: there are no mints in
// this header — `Graded<M, L, T>` is the SUBSTRATE that the safety
// wrappers (Linear, Refined, Tagged, Secret, ...) decorate.  These
// re-exports are pure typename aliases preserving template identity.
// Every alias `fixy::algebra::Graded<M, L, T>` IS
// `algebra::Graded<M, L, T>` (same template, same instantiations,
// same EBO behaviour).
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   algebra/Graded.h           — Graded<M, L, T> core
//   algebra/GradedTrait.h      — GradedWrapper concept, regime taxonomy
//   algebra/Lattice.h          — Lattice / Semiring concepts +
//                                law-verifier helpers
//   algebra/Modality.h         — ModalityKind enum, modality::* tags
//   algebra/lattices/AllLattices.h
//                              — pulls in all 30 concrete lattices
//
// ── Lattice catalog (30 entries, Phase C+ in flight) ───────────────
//
//   QttSemiring, BoolLattice<Pred>, ConfLattice, TrustLattice<Src>,
//   FractionalLattice, MonotoneLattice<T,Cmp>, SeqPrefixLattice<E>,
//   StalenessSemiring, HappensBeforeLattice<N,Tag>, LifetimeLattice,
//   ConsistencyLattice, ToleranceLattice, ProductLattice<Ls...>,
//   AffinityLattice, AllocClassLattice, BitsBudgetLattice,
//   CipherTierLattice, CrashLattice, DetSafeLattice, EpochLattice,
//   GenerationLattice, HotPathLattice, MemOrderLattice,
//   NumaNodeLattice, PeakBytesLattice, ProgressLattice,
//   RecipeFamilyLattice, ResidencyHeatLattice, VendorLattice,
//   WaitLattice.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — Graded's NSDMI discipline carries through the alias.
//   TypeSafe — using-declarations preserve template identity; concept
//              substitution at call sites resolves to the substrate's
//              concept definition (Lattice / Semiring / BoundedLattice).
//   NullSafe — graded wrappers carry no pointer state.
//   MemSafe  — Graded's [[no_unique_address]] EBO behaviour is a
//              property of the type definition, not the alias —
//              `sizeof(fixy::algebra::Linear<int>)` is identical to
//              `sizeof(safety::Linear<int>)`.
//   DetSafe  — algebraic operations on lattice elements are constexpr;
//              same compile-time result regardless of include path.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Template aliases ARE the substrate templates; zero new
// instantiations introduced, zero new symbols emitted.

#include <crucible/algebra/Algebra.h>      // pulls Graded + Lattice +
                                           // Modality + AllLattices
#include <crucible/algebra/GradedTrait.h>  // GradedWrapper concept +
                                           // IsGraded probe

namespace crucible::fixy::algebra {

// ── Modality enum + per-form tags ──────────────────────────────────

using ::crucible::algebra::ModalityKind;

// Modality concept gates — predicate-form substitution checks.
using ::crucible::algebra::IsModality;
using ::crucible::algebra::ComonadModality;
using ::crucible::algebra::RelativeMonadModality;
using ::crucible::algebra::AbsoluteModality;
using ::crucible::algebra::RelativeModality;
using ::crucible::algebra::QuotientModality;
using ::crucible::algebra::CoeffectModality;

// Compile-time predicates: has_counit_v / has_unit_v / has_grade_only_v.
template <ModalityKind K>
inline constexpr bool has_counit_v     = ::crucible::algebra::has_counit_v<K>;
template <ModalityKind K>
inline constexpr bool has_unit_v       = ::crucible::algebra::has_unit_v<K>;
template <ModalityKind K>
inline constexpr bool has_grade_only_v = ::crucible::algebra::has_grade_only_v<K>;

// modality::Comonad_t / RelativeMonad_t / Absolute_t / Relative_t /
// Quotient_t / Coeffect_t — overload-set dispatch tag types.
namespace modality = ::crucible::algebra::modality;

// Reflection-derived cardinality of ModalityKind.
inline constexpr std::size_t modality_kind_count =
    ::crucible::algebra::modality_kind_count;

using ::crucible::algebra::modality_name;

// ── Core Graded substrate ──────────────────────────────────────────
//
// `Graded<Modality, Lattice, T>` — the single template every safety
// wrapper folds into per 25_04_2026.md §2.  Five regimes per
// GradedTrait.h: empty-grade EBO collapse, T==element_type collapse,
// derived-from-container, T+grade-per-instance, proof-token.

template <ModalityKind M, typename L, typename T>
using Graded = ::crucible::algebra::Graded<M, L, T>;

// ── GradedWrapper concept + diagnostic accessors ───────────────────
//
// Wrapper conformance gate: presence of value_type / graded_type /
// value_type_name() / lattice_name() forwarders.

using ::crucible::algebra::GradedWrapper;

// IsGraded<W> — wrapper-level "wraps Graded<...>" probe; mirrors
// the substrate predicate so fixy:: callers do not need to spell the
// algebra:: namespace.
using ::crucible::algebra::IsGraded;

// ── Lattice / Semiring concepts ────────────────────────────────────

using ::crucible::algebra::Lattice;
using ::crucible::algebra::Semiring;
using ::crucible::algebra::BoundedLattice;
using ::crucible::algebra::BoundedBelowLattice;
using ::crucible::algebra::BoundedAboveLattice;
using ::crucible::algebra::UnboundedLattice;
using ::crucible::algebra::HasLatticeName;

// LatticeElement<L> — element_type projection.
template <typename L>
using LatticeElement = ::crucible::algebra::LatticeElement<L>;

using ::crucible::algebra::lattice_name;

// Subsumption helpers.
using ::crucible::algebra::subsumes;
using ::crucible::algebra::equivalent;
using ::crucible::algebra::strictly_less;

// Law verifiers (consteval — used by lattice authors in self-test
// blocks).  Re-exported so fixy::-side test fixtures can audit a
// custom lattice without descending into algebra/.
using ::crucible::algebra::verify_idempotent_join;
using ::crucible::algebra::verify_idempotent_meet;
using ::crucible::algebra::verify_commutative_join;
using ::crucible::algebra::verify_commutative_meet;
using ::crucible::algebra::verify_associative_join;
using ::crucible::algebra::verify_associative_meet;
using ::crucible::algebra::verify_absorption;
using ::crucible::algebra::verify_partial_order;
using ::crucible::algebra::verify_bottom_identity;
using ::crucible::algebra::verify_top_identity;
using ::crucible::algebra::verify_lattice_axioms_at;
using ::crucible::algebra::verify_bounded_lattice_axioms_at;
using ::crucible::algebra::verify_distributive_lattice;
using ::crucible::algebra::verify_additive_identity;
using ::crucible::algebra::verify_multiplicative_identity;
using ::crucible::algebra::verify_multiplicative_zero;
using ::crucible::algebra::verify_additive_commutative;
using ::crucible::algebra::verify_additive_associative;
using ::crucible::algebra::verify_multiplicative_associative;
using ::crucible::algebra::verify_distributivity;
using ::crucible::algebra::verify_semiring_axioms_at;

// ── lattices::* — 30 concrete instantiations ──────────────────────
//
// Inline alias namespace — every `fixy::algebra::lattices::X` IS
// `algebra::lattices::X` (template identity preserved).

namespace lattices = ::crucible::algebra::lattices;

}  // namespace crucible::fixy::algebra

// ── Self-test ──────────────────────────────────────────────────────
//
// Witness that the alias preserves Graded's substrate identity and
// every lattice's element-type plus its conformance to the Lattice /
// Semiring concept.  Full coverage in test_fixy_algebra.cpp.

namespace crucible::fixy::algebra::self_test {

// Substrate identity.
static_assert(std::is_same_v<
    Graded<ModalityKind::Absolute, lattices::QttSemiring::At<lattices::QttGrade::One>, int>,
    ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        ::crucible::algebra::lattices::QttSemiring::At<
            ::crucible::algebra::lattices::QttGrade::One>,
        int>>,
    "fixy::algebra::Graded must alias algebra::Graded");

// Concept passes through unchanged.
static_assert(Lattice<lattices::QttSemiring>);
static_assert(Lattice<lattices::FractionalLattice>);
static_assert(Lattice<lattices::ConfLattice>);
static_assert(Semiring<lattices::QttSemiring>);
static_assert(Semiring<lattices::StalenessSemiring>);

// Modality enum passes through unchanged.
static_assert(ModalityKind::Absolute      == ::crucible::algebra::ModalityKind::Absolute);
static_assert(ModalityKind::Comonad       == ::crucible::algebra::ModalityKind::Comonad);
static_assert(ModalityKind::RelativeMonad == ::crucible::algebra::ModalityKind::RelativeMonad);
static_assert(modality_kind_count == 6,
    "fixy::algebra::modality_kind_count must match the substrate's six-enumerator catalog");

}  // namespace crucible::fixy::algebra::self_test
