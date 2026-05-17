// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-ALGEBRA fixture #5: GradedWrapper concept rejects a wrapper
// that omits the `value_type_name()` consteval forwarder.
//
// Violation: routing through `fixy::algebra::GradedWrapper<W>` must
// reject `IncompleteGradedWrapper` because GradedWrapper requires
// `{ W::value_type_name() } noexcept -> std::same_as<std::string_view>`
// — the diagnostic forwarder is load-bearing for safety-wrapper error
// messages and the cheat-probe (CHEAT-3) audit.
//
// Distinct from fixtures #1-#4:
//   #1 non_lattice          — rejects on Lattice concept (substrate)
//   #2 non_graded           — rejects on IsGraded (specialization probe)
//   #3 semiring_missing_zero — rejects on Semiring (additive identity)
//   #4 bounded_missing_top   — rejects on BoundedLattice (top())
//   #5 gw_missing_name       — rejects on GradedWrapper (forwarder)
//
// This fixture is the diagnostic-surface gate.  A wrapper missing
// value_type_name() would emit lying or empty type names in error
// messages; the GradedWrapper concept's CHEAT-3 clause catches the
// omission at the type-system layer so no degraded wrapper ever
// reaches the diagnostic-emission code path.
//
// Expected diagnostic: GCC's "associated constraints are not satisfied"
// pointing at the GradedWrapper concept's value_type_name() expression.

#include <crucible/fixy/Algebra.h>

#include <string_view>

namespace fa = crucible::fixy::algebra;

// A "wrapper" that publishes value_type / lattice_type / graded_type
// but OMITS value_type_name() and lattice_name() forwarders.  Has the
// graded_type alias pointing at a real Graded<> specialization so the
// is_graded_specialization_v clause does NOT short-circuit the
// rejection — the rejection must occur on the missing-forwarder clause.
struct AlgebraNegFixture5_NoForwarders {
    using graded_t  = fa::Graded<
        fa::ModalityKind::Absolute,
        fa::lattices::QttSemiring::At<fa::lattices::QttGrade::One>,
        int>;
    using value_type   = int;
    using lattice_type = fa::lattices::QttSemiring;
    using graded_type  = graded_t;

    static constexpr fa::ModalityKind modality = fa::ModalityKind::Absolute;

    // INTENTIONALLY MISSING:
    //   static consteval std::string_view value_type_name() noexcept;
    //   static consteval std::string_view lattice_name()    noexcept;
};

int main() {
    // GradedWrapper concept must reject — missing diagnostic forwarders.
    static_assert(fa::GradedWrapper<AlgebraNegFixture5_NoForwarders>,
        "fa::GradedWrapper<NoForwarders> must reject — missing the "
        "value_type_name() / lattice_name() consteval forwarders.  The "
        "fixy::algebra alias preserves the substrate's concept gate; a "
        "wrapper without the diagnostic-forwarder pair cannot serve the "
        "safety-wrapper error-emission contract.");
    return 0;
}
