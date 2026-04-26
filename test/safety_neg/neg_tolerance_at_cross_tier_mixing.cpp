// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a ToleranceLattice::At<TIER_B>::element_type
// to a function expecting ToleranceLattice::At<TIER_A>::element_type.
//
// Symmetric to neg_lifetime_at_cross_tier_mixing.cpp and
// neg_consistency_at_cross_tier_mixing.cpp but pinned across the
// THIRD of the new chain-lattice families.  Together the three
// neg-compiles (Lifetime, Consistency, Tolerance) cover all three
// At<>-based chain lattices and would each catch a refactor that
// drifted just that family's element_type identity.
//
// Concrete bug-class this catches: a §10 precision-budget calibrator
// regression where two ops at adjacent tolerance tiers (e.g.
// ULP_FP16 and ULP_FP32) silently shared element_type and let a
// per-op grade flow across without triggering the LP allocator's
// re-evaluation.  The neg-compile guarantees the lattice substrate
// keeps the two tiers as distinct types.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity.

#include <crucible/algebra/lattices/ToleranceLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    ToleranceLattice::At<Tolerance::RELAXED>::element_type relaxed_elt{};
    ToleranceLattice::At<Tolerance::BITEXACT>::element_type bitexact_elt{};

    // Should FAIL: At<RELAXED>::leq expects two At<RELAXED>::
    // element_type arguments; bitexact_elt is At<BITEXACT>::
    // element_type — different template instantiation, different
    // type, no implicit conversion.
    return static_cast<int>(
        ToleranceLattice::At<Tolerance::RELAXED>::leq(relaxed_elt, bitexact_elt));
}
