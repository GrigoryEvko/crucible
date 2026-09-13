#pragma once

#include <crucible/algebra/Algebra.h>
#include <crucible/algebra/GradedTrait.h>
#include <crucible/safety/DimensionTraits.h>

namespace crucible::fixy::algebra {

using ::crucible::algebra::ModalityKind;

using ::crucible::algebra::IsModality;
using ::crucible::algebra::ComonadModality;
using ::crucible::algebra::RelativeMonadModality;
using ::crucible::algebra::AbsoluteModality;
using ::crucible::algebra::RelativeModality;
using ::crucible::algebra::QuotientModality;
using ::crucible::algebra::CoeffectModality;

template <ModalityKind K>
inline constexpr bool has_counit_v = ::crucible::algebra::has_counit_v<K>;
template <ModalityKind K>
inline constexpr bool has_unit_v = ::crucible::algebra::has_unit_v<K>;
template <ModalityKind K>
inline constexpr bool has_grade_only_v = ::crucible::algebra::has_grade_only_v<K>;

namespace modality = ::crucible::algebra::modality;

inline constexpr std::size_t modality_kind_count = ::crucible::algebra::modality_kind_count;

using ::crucible::algebra::modality_name;

template <ModalityKind M, typename L, typename T>
using Graded = ::crucible::algebra::Graded<M, L, T>;

using ::crucible::algebra::GradedWrapper;

// The two probes ask different questions. GradedWrapper admits a class that
// wraps a Graded. IsGraded is strict identity and admits only a Graded
// specialization itself, so it reports false for every such wrapper.
using ::crucible::algebra::IsGraded;

using ::crucible::algebra::Lattice;
using ::crucible::algebra::Semiring;
using ::crucible::algebra::BoundedLattice;
using ::crucible::algebra::BoundedBelowLattice;
using ::crucible::algebra::BoundedAboveLattice;
using ::crucible::algebra::UnboundedLattice;
using ::crucible::algebra::HasLatticeName;

template <typename L>
using LatticeElement = ::crucible::algebra::LatticeElement<L>;

using ::crucible::algebra::lattice_name;

using ::crucible::algebra::subsumes;
using ::crucible::algebra::equivalent;
using ::crucible::algebra::strictly_less;

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

namespace lattices = ::crucible::algebra::lattices;

// These accessors answer which axis a wrapper's grade lives on, rather than
// naming the axis itself. They depend on the graded-wrapper concept and the
// modality enum, so they nest inside the algebra family. The pure axis
// taxonomy stays outside it and compiles with no algebra dependency.
//
// Each accessor is a using-declaration, not a redefined template. Partial
// specializations therefore propagate through the name, and a wrapper
// specialization added elsewhere becomes reachable here with no further edit.
// A wrapping variable template would instead capture a closed enumeration and
// lose template-template argument matching.

namespace dim {

using ::crucible::safety::DimensionedGradedWrapper;

using ::crucible::safety::wrapper_dimension;
using ::crucible::safety::wrapper_dimension_v;

using ::crucible::safety::wrapper_tier_v;

using ::crucible::safety::wrapper_modality_v;

using ::crucible::safety::wrapper_lattice_t;

using ::crucible::safety::tier_for_grade;
using ::crucible::safety::tier_for_grade_v;
using ::crucible::safety::dimension_tier_v;

using ::crucible::safety::verify_quadruple;

}  // namespace dim

}  // namespace crucible::fixy::algebra

namespace crucible::fixy::algebra::self_test {

static_assert(
    std::is_same_v<
        Graded<ModalityKind::Absolute, lattices::QttSemiring::At<lattices::QttGrade::One>, int>,
        ::crucible::algebra::Graded<
            ::crucible::algebra::ModalityKind::Absolute,
            ::crucible::algebra::lattices::QttSemiring::At<::crucible::algebra::lattices::QttGrade::One>, int>>,
    "fixy::algebra::Graded must alias algebra::Graded");

static_assert(Lattice<lattices::QttSemiring>);
static_assert(Lattice<lattices::FractionalLattice>);
static_assert(Lattice<lattices::ConfLattice>);
static_assert(Semiring<lattices::QttSemiring>);
static_assert(Semiring<lattices::StalenessSemiring>);

static_assert(ModalityKind::Absolute == ::crucible::algebra::ModalityKind::Absolute);
static_assert(ModalityKind::Comonad == ::crucible::algebra::ModalityKind::Comonad);
static_assert(ModalityKind::RelativeMonad == ::crucible::algebra::ModalityKind::RelativeMonad);

// Both sides derive from the same enum, so the comparison is tautological
// while modality_kind_count stays a reflection query. It catches the drift
// case where that constant is replaced by a hand-maintained literal.
static_assert(modality_kind_count == std::meta::enumerators_of(^^::crucible::algebra::ModalityKind).size(),
              "algebra::modality_kind_count has drifted from the reflection-derived "
              "enumerator count of algebra::ModalityKind.  Either the constant is "
              "hand-maintained and was not bumped when an enumerator was added, or "
              "reflection is reporting a different enum than the substrate exposes.  "
              "The constant must remain a reflection query over ModalityKind.");

static_assert(IsGraded<Graded<ModalityKind::Absolute, lattices::QttSemiring::At<lattices::QttGrade::One>, int>>,
              "fixy::algebra::IsGraded must accept Graded<...> specializations.");
static_assert(!IsGraded<int>, "fixy::algebra::IsGraded must reject bare types — strict identity, "
                              "not structural / wrapper-level (the latter is GradedWrapper's role).");

}  // namespace crucible::fixy::algebra::self_test

namespace crucible::fixy::algebra::dim::self_test {

namespace fad = ::crucible::fixy::algebra::dim;
namespace ssaf = ::crucible::safety;

// One representative wrapper per tier.
using WRefined = ssaf::Refined<ssaf::positive, int>;
using WLinear = ssaf::Linear<int>;
using WTagged = ssaf::Tagged<int, ssaf::source::FromUser>;
struct TimeTag {};
using WTimeOrdered = ssaf::TimeOrdered<int, 4, TimeTag>;
using WEpochVersioned = ssaf::EpochVersioned<int>;

static_assert(fad::DimensionedGradedWrapper<WRefined>);
static_assert(fad::DimensionedGradedWrapper<WLinear>);
static_assert(fad::DimensionedGradedWrapper<WTagged>);
static_assert(fad::DimensionedGradedWrapper<WTimeOrdered>);
static_assert(fad::DimensionedGradedWrapper<WEpochVersioned>);
static_assert(!fad::DimensionedGradedWrapper<int>);
static_assert(!fad::DimensionedGradedWrapper<void*>);

static_assert(fad::wrapper_dimension_v<WRefined> == ssaf::wrapper_dimension_v<WRefined>);
static_assert(fad::wrapper_dimension_v<WLinear> == ssaf::wrapper_dimension_v<WLinear>);
static_assert(fad::wrapper_dimension_v<WTagged> == ssaf::wrapper_dimension_v<WTagged>);
static_assert(fad::wrapper_dimension_v<WTimeOrdered> == ssaf::wrapper_dimension_v<WTimeOrdered>);
static_assert(fad::wrapper_dimension_v<WEpochVersioned> == ssaf::wrapper_dimension_v<WEpochVersioned>);

static_assert(fad::wrapper_tier_v<WLinear> == ssaf::TierKind::Semiring);
static_assert(fad::wrapper_tier_v<WRefined> == ssaf::TierKind::Foundational);
static_assert(fad::wrapper_tier_v<WTagged> == ssaf::TierKind::Semiring);
static_assert(fad::wrapper_tier_v<WTimeOrdered> == ssaf::TierKind::Lattice);
static_assert(fad::wrapper_tier_v<WEpochVersioned> == ssaf::TierKind::Versioned);

static_assert(fad::wrapper_modality_v<WLinear> == ssaf::wrapper_modality_v<WLinear>);
static_assert(fad::wrapper_modality_v<WTagged> == ssaf::wrapper_modality_v<WTagged>);

static_assert(std::is_same_v<typename fad::wrapper_lattice_t<WLinear>, typename ssaf::wrapper_lattice_t<WLinear>>);
static_assert(
    std::is_same_v<typename fad::wrapper_lattice_t<WTimeOrdered>, typename ssaf::wrapper_lattice_t<WTimeOrdered>>);

// tier_for_grade classifies a bare grade type with no wrapper. An int has no
// lattice, semiring, typestate or versioned shape, so it lands on the
// foundational tier.
static_assert(fad::tier_for_grade_v<int> == ssaf::TierKind::Foundational);

// The substrate already verifies every shipped wrapper. These five samples
// confirm the consteval body still runs through the alias without losing
// template-argument deduction or modality matching.
static_assert(fad::verify_quadruple<WLinear>());
static_assert(fad::verify_quadruple<WRefined>());
static_assert(fad::verify_quadruple<WTagged>());
static_assert(fad::verify_quadruple<WTimeOrdered>());
static_assert(fad::verify_quadruple<WEpochVersioned>());

// dimension_tier_v classifies a wrapper's lattice through the heuristic grade
// path. wrapper_tier_v above instead reads the wrapper's exact dimension
// specialization, so the two can disagree.
static_assert(fad::dimension_tier_v<WLinear> == ssaf::dimension_tier_v<WLinear>);

}  // namespace crucible::fixy::algebra::dim::self_test
