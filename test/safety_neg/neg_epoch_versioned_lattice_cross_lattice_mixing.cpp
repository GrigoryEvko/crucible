// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a Generation value to EpochLattice::leq.
//
// Pins the structural disjointness of the two version-axis lattices
// at the LATTICE substrate level, BELOW the wrapper.  Both
// EpochLattice and GenerationLattice carry uint64_t-backed elements,
// but their element_types are distinct strong-typed newtypes (Epoch
// and Generation).  Cross-lattice mixing must be rejected.
//
// [GCC-WRAPPER-TEXT] — leq parameter-type mismatch on the strong
// newtype carrier.

#include <crucible/algebra/lattices/EpochLattice.h>
#include <crucible/algebra/lattices/GenerationLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    Epoch      ep{1024};
    Generation gen{4096};

    // Should FAIL: EpochLattice::leq's signature requires two
    // Epoch arguments; passing a Generation as the second argument
    // is a type mismatch — even though both are uint64_t-backed.
    return static_cast<int>(EpochLattice::leq(ep, gen));
}
