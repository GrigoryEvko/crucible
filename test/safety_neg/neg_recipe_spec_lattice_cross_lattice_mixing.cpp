// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a RecipeFamily value to ToleranceLattice::leq.
//
// Pins the structural disjointness of the two RecipeSpec-axis
// lattices at the LATTICE substrate level.  Both Tolerance and
// RecipeFamily are uint8_t-backed strong scoped enums but
// structurally distinct.  Cross-lattice mixing rejected.
//
// [GCC-WRAPPER-TEXT] — leq parameter-type mismatch on the strong
// scoped enum carrier.

#include <crucible/algebra/lattices/RecipeFamilyLattice.h>
#include <crucible/algebra/lattices/ToleranceLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    Tolerance    tier{Tolerance::ULP_FP16};
    RecipeFamily fam{RecipeFamily::Kahan};

    // Should FAIL: ToleranceLattice::leq requires two Tolerance
    // values; passing a RecipeFamily is a type mismatch.
    return static_cast<int>(ToleranceLattice::leq(tier, fam));
}
