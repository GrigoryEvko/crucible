// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a ProgressLattice::At<CLASS_B>::element_type to
// a function expecting ProgressLattice::At<CLASS_A>::element_type.
//
// Per-At<T> nested-struct-identity at the LATTICE substrate.
// Mirrors neg_mem_order_at_cross_tag_mixing /
// neg_wait_at_cross_strategy_mixing / etc.  A future refactor
// extracting a shared singleton_carrier<ProgressClass> alias above
// At<T> would silently allow Bounded-class and MayDiverge-class
// values to interconvert at the lattice level.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/ProgressLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    ProgressLattice::At<ProgressClass::Bounded>::element_type    bounded_elt{};
    ProgressLattice::At<ProgressClass::MayDiverge>::element_type diverge_elt{};

    // Should FAIL: At<Bounded>::leq expects two At<Bounded>::
    // element_type arguments; diverge_elt is At<MayDiverge>::
    // element_type.
    return static_cast<int>(
        ProgressLattice::At<ProgressClass::Bounded>::leq(bounded_elt, diverge_elt));
}
