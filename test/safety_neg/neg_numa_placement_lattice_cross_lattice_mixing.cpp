// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing an AffinityMask value to NumaNodeLattice::leq.
//
// Pins the structural disjointness of the two NumaPlacement-axis
// lattices at the LATTICE substrate level.  NumaNodeLattice's
// element_type is NumaNodeId (1-byte enum); AffinityLattice's is
// AffinityMask (32-byte struct).  Cross-lattice mixing rejected.
//
// [GCC-WRAPPER-TEXT] — leq parameter-type mismatch.

#include <crucible/algebra/lattices/AffinityLattice.h>
#include <crucible/algebra/lattices/NumaNodeLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    NumaNodeId   node{2};
    AffinityMask aff = AffinityMask::single(0);

    // Should FAIL: NumaNodeLattice::leq requires two NumaNodeId
    // values; passing an AffinityMask is a type mismatch.
    return static_cast<int>(NumaNodeLattice::leq(node, aff));
}
