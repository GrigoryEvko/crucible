#pragma once

// ── crucible::fixy::algebra — Graded substrate + 30 lattices ───────
//
// Re-export per misc/16_05_2026_fixy.md.  Surfaces the full
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
// ── Lattice catalog (30 entries, in flight) ────────────────────────
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
#include <crucible/safety/DimensionTraits.h>  // FIXY-U-061: wrapper_*
                                              // accessors + verify_quadruple
                                              // (transitively pulls every
                                              // safety/ wrapper that ships a
                                              // wrapper_dimension<W>
                                              // partial specialization)

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

// IsGraded<T> — STRICT-IDENTITY probe: true iff T is literally a
// `Graded<M, L, V>` specialization (with cv-ref symmetry).  Distinct
// from `GradedWrapper` above, which admits CLASSES wrapping Graded
// (Linear<T>, Refined<P, T>, ...) — those are not Graded specializa-
// tions themselves and `IsGraded` reports false for them.  Substrate
// reference: algebra/Graded.h:1353-1387 (the FIXY-G*-AUDIT block).
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
// Namespace alias — every `fixy::algebra::lattices::X` IS
// `algebra::lattices::X` (template identity preserved).  This is a
// plain (non-inline) namespace alias; there is no inline-namespace
// promotion of contents into the enclosing scope.

namespace lattices = ::crucible::algebra::lattices;

// ── dim:: — wrapper-side dimension/tier/modality accessors ─────────
//
// FIXY-U-061 (#1744).  The 21-axis × 5-Tier × 6-Modality classification
// catalog lives in safety/DimensionTraits.h.  fixy::dim:: (separate
// header) re-exports the AXIS-side surface — the enums, name lookups,
// and tier classification of an axis.  This sub-namespace re-exports
// the WRAPPER-side surface — what dimension a Graded-backed wrapper
// covers, what tier its lattice falls under, what modality it carries,
// and the cross-axis quadruple verifier that ships per-wrapper sentinels.
//
// Why nested under fixy::algebra::dim instead of fixy::dim:
//
//   fixy::dim::DimensionAxis::Usage         — the axis itself (taxonomy)
//   fixy::algebra::dim::wrapper_dimension<W> — which axis the wrapper
//                                              W's grade lives on
//
// The wrapper-side accessors depend on `algebra::GradedWrapper` and
// `algebra::ModalityKind`, so they belong inside the algebra family.
// fixy::dim:: pure-axis taxonomy compiles standalone (no algebra
// dependency); fixy::algebra::dim:: depends on the whole algebra tree.
//
// ── Re-export discipline (§XXI grep-target preservation) ───────────
//
// Each accessor is a using-decl alias, NOT a re-defined template.
// This means:
//   (a) `fixy::algebra::dim::wrapper_dimension<Linear<int>>` IS
//       `safety::wrapper_dimension<safety::Linear<int>>` — partial
//       specializations propagate through the alias because using-decl
//       is name aliasing.
//   (b) `wrapper_dimension_v<W>` is the SAME variable template, not a
//       wrapping `inline constexpr auto wrapper_dimension_v = ...`
//       which would lose template-template parameter matching.
//   (c) Adding a new wrapper specialization to safety/DimensionTraits.h
//       makes the new wrapper instantly reachable through fixy::algebra::
//       dim:: with zero further work in this header — the alias does
//       not capture a closed enumeration of specializations.
//
// ── Self-test (within this header) ─────────────────────────────────
//
// `crucible::fixy::algebra::dim::self_test::*` static_asserts pin five
// representative wrappers (Linear/Refined/Tagged/TimeOrdered/EpochVersioned
// covering Tier-S / Tier-F / Tier-S / Tier-L / Tier-V respectively)
// such that:
//   - the alias preserves wrapper_dimension's specialization matching
//   - wrapper_tier_v through the alias agrees with substrate's per-wrapper
//     answer
//   - wrapper_modality_v survives the alias
//   - verify_quadruple<W>() runs through the alias and returns true on
//     wrappers the substrate self-test passes
//   - DimensionedGradedWrapper concept accepts what's classifiable and
//     rejects bare types
//
// These are the load-bearing identity-witness assertions for FIXY-U-061.
// Cross-coverage with substrate's own DimensionTraits.h self-test gives
// belt-and-suspenders defense against an alias break.

namespace dim {

// ── Concept gate ───────────────────────────────────────────────────
using ::crucible::safety::DimensionedGradedWrapper;

// ── Wrapper-side dimension accessor (axis discovery) ───────────────
using ::crucible::safety::wrapper_dimension;
using ::crucible::safety::wrapper_dimension_v;

// ── Wrapper-side tier accessor (exact tier from wrapper_dimension) ─
using ::crucible::safety::wrapper_tier_v;

// ── Wrapper-side modality accessor ─────────────────────────────────
using ::crucible::safety::wrapper_modality_v;

// ── Wrapper-side lattice projection ────────────────────────────────
using ::crucible::safety::wrapper_lattice_t;

// ── Best-effort tier classification from a grade type ──────────────
using ::crucible::safety::tier_for_grade;
using ::crucible::safety::tier_for_grade_v;
using ::crucible::safety::dimension_tier_v;

// ── Cross-axis quadruple verifier ──────────────────────────────────
using ::crucible::safety::verify_quadruple;

}  // namespace dim

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

// fixy-A4-012: substrate-cross-check.  Mirrors fixy/Dim.h:140-147 — the
// canonical post-fixy-H-12 pattern.  The literal `== 6` tripwire lives
// at the substrate (algebra/Modality.h:170) where it forces a developer
// adding a 7th modality to update modality_name()'s switch arms.  This
// fixy-side check catches the SEPARATE structural-drift case where the
// substrate constant is ever redefined non-reflectively (manual count,
// hardcoded literal — feedback_gcc16_c26_reflection_gotchas.md §3) and
// falls out of sync with the enum's reflection-derived cardinality.
// Both sides of the comparison derive from the same enum, so under
// normal operation this assertion is tautological — adding a new
// modality enumerator bumps both sides together.
static_assert(modality_kind_count
              == std::meta::enumerators_of(^^::crucible::algebra::ModalityKind).size(),
    "fixy::algebra — substrate algebra::modality_kind_count has drifted "
    "from the reflection-derived enumerator count of algebra::ModalityKind.  "
    "Either the substrate constant was manually maintained (and forgot to "
    "bump on enumerator addition) or reflection is reporting a different "
    "enum than the substrate exposes.  Investigate algebra/Modality.h:74 — "
    "it MUST remain `std::meta::enumerators_of(^^ModalityKind).size()`.");

// fixy-M-01: IsGraded strict-identity witness.  The fixy doc-block
// previously characterised IsGraded as a "wrapper-level" probe, which
// is GradedWrapper's role.  IsGraded asks the orthogonal question:
// "is T LITERALLY a Graded<M, L, V> specialization?" — see Graded.h
// :1365-1366.  These two witnesses lock the strict-identity reading
// down: a true Graded passes, a bare value type fails.
static_assert(IsGraded<Graded<ModalityKind::Absolute,
                              lattices::QttSemiring::At<lattices::QttGrade::One>,
                              int>>,
    "fixy::algebra::IsGraded must accept Graded<...> specializations.");
static_assert(!IsGraded<int>,
    "fixy::algebra::IsGraded must reject bare types — strict identity, "
    "not structural / wrapper-level (the latter is GradedWrapper's role).");

}  // namespace crucible::fixy::algebra::self_test

// ─────────────────────────────────────────────────────────────────────
// ── fixy::algebra::dim self-test — FIXY-U-061 alias-identity witness
// ─────────────────────────────────────────────────────────────────────
//
// Pins five representative wrappers (one per Tier S/L/T-irrelevant/F/V)
// so a future regression in the using-decl re-export breaks compilation
// rather than silently producing a different answer than the substrate.

namespace crucible::fixy::algebra::dim::self_test {

namespace fad = ::crucible::fixy::algebra::dim;
namespace ssaf = ::crucible::safety;

// Tier-F: Refinement.  Refined<positive, int> — `positive` is the
// canonical predicate lambda from safety/Refined.h:71.
using WRefined = ssaf::Refined<ssaf::positive, int>;

// Tier-S: Usage (linear).  Linear<int>.
using WLinear  = ssaf::Linear<int>;

// Tier-S: Provenance.  Tagged<int, source::FromUser>.
using WTagged  = ssaf::Tagged<int, ssaf::source::FromUser>;

// Tier-L: Representation.  TimeOrdered<int, 4, Tag>.
struct TimeTag {};
using WTimeOrdered = ssaf::TimeOrdered<int, 4, TimeTag>;

// Tier-V: Version.  EpochVersioned<int>.
using WEpochVersioned = ssaf::EpochVersioned<int>;

// ── Concept gate: classifiable wrappers admit, bare types reject ───
static_assert(fad::DimensionedGradedWrapper<WRefined>);
static_assert(fad::DimensionedGradedWrapper<WLinear>);
static_assert(fad::DimensionedGradedWrapper<WTagged>);
static_assert(fad::DimensionedGradedWrapper<WTimeOrdered>);
static_assert(fad::DimensionedGradedWrapper<WEpochVersioned>);
static_assert(!fad::DimensionedGradedWrapper<int>);
static_assert(!fad::DimensionedGradedWrapper<void*>);

// ── wrapper_dimension_v through the alias agrees with substrate ────
static_assert(fad::wrapper_dimension_v<WRefined>
              == ssaf::wrapper_dimension_v<WRefined>);
static_assert(fad::wrapper_dimension_v<WLinear>
              == ssaf::wrapper_dimension_v<WLinear>);
static_assert(fad::wrapper_dimension_v<WTagged>
              == ssaf::wrapper_dimension_v<WTagged>);
static_assert(fad::wrapper_dimension_v<WTimeOrdered>
              == ssaf::wrapper_dimension_v<WTimeOrdered>);
static_assert(fad::wrapper_dimension_v<WEpochVersioned>
              == ssaf::wrapper_dimension_v<WEpochVersioned>);

// ── wrapper_tier_v hits the exact tier per substrate self-test ─────
static_assert(fad::wrapper_tier_v<WLinear>          == ssaf::TierKind::Semiring);
static_assert(fad::wrapper_tier_v<WRefined>         == ssaf::TierKind::Foundational);
static_assert(fad::wrapper_tier_v<WTagged>          == ssaf::TierKind::Semiring);
static_assert(fad::wrapper_tier_v<WTimeOrdered>     == ssaf::TierKind::Lattice);
static_assert(fad::wrapper_tier_v<WEpochVersioned>  == ssaf::TierKind::Versioned);

// ── wrapper_modality_v survives the alias ──────────────────────────
static_assert(fad::wrapper_modality_v<WLinear>
              == ssaf::wrapper_modality_v<WLinear>);
static_assert(fad::wrapper_modality_v<WTagged>
              == ssaf::wrapper_modality_v<WTagged>);

// ── wrapper_lattice_t projects to the same lattice type ────────────
static_assert(std::is_same_v<
    typename fad::wrapper_lattice_t<WLinear>,
    typename ssaf::wrapper_lattice_t<WLinear>>);
static_assert(std::is_same_v<
    typename fad::wrapper_lattice_t<WTimeOrdered>,
    typename ssaf::wrapper_lattice_t<WTimeOrdered>>);

// ── tier_for_grade_v works for bare grade types ────────────────────
//
// Per safety/DimensionTraits.h:382, tier_for_grade<G> classifies a bare
// grade type without needing a wrapper.  An int defaults to Foundational
// (no Lattice/Semiring/Typestate/Versioned shape).
static_assert(fad::tier_for_grade_v<int> == ssaf::TierKind::Foundational);

// ── verify_quadruple<W>() returns true on every shipped wrapper ────
//
// The substrate runs this for 26 wrappers (DimensionTraits.h:856-).
// We pin five representative samples to confirm the alias preserves
// consteval semantics — the consteval body runs through the using-decl
// without losing template-argument deduction or modality matching.
static_assert(fad::verify_quadruple<WLinear>());
static_assert(fad::verify_quadruple<WRefined>());
static_assert(fad::verify_quadruple<WTagged>());
static_assert(fad::verify_quadruple<WTimeOrdered>());
static_assert(fad::verify_quadruple<WEpochVersioned>());

// ── dimension_tier_v (best-effort classification) survives alias ──
//
// dimension_tier_v<W> classifies a wrapper's lattice via tier_for_grade
// (the heuristic path).  Differs from wrapper_tier_v above which reads
// from the wrapper's exact dimension specialization.
static_assert(fad::dimension_tier_v<WLinear>
              == ssaf::dimension_tier_v<WLinear>);

}  // namespace crucible::fixy::algebra::dim::self_test
