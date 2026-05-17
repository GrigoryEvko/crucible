// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-ALGEBRA fixture #1: Lattice concept rejects a non-lattice
// substrate when routed through `fixy::algebra::Lattice`.
//
// Violation: instantiating `Graded<Absolute, BadLattice, int>` via the
// fixy::algebra alias must reject because BadLattice publishes no
// `element_type`, no `leq` / `join` / `meet` — failing the Lattice
// concept template-substitution gate.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at the Lattice concept (or its underlying
// requires-clause `typename L::element_type`).

#include <crucible/fixy/Algebra.h>

namespace fa = crucible::fixy::algebra;

// Carrier type unique to this fixture — avoids cross-fixture overload
// resolution interference.
struct AlgebraNegFixture1_BadLattice {};

int main() {
    // Routing through fixy::algebra::Lattice must reject BadLattice.
    static_assert(fa::Lattice<AlgebraNegFixture1_BadLattice>,
        "fa::Lattice<BadLattice> must reject — BadLattice has no "
        "element_type / leq / join / meet.  The fixy alias preserves "
        "the substrate's concept gate.");
    return 0;
}
