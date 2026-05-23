// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-252 BarrierStrengthLattice, mismatch class #2 of 2:
// At<K> SINGLETON GRADE DISJOINTNESS.
//
// Each lattice's `At<K>::element_type` is a DISTINCT empty struct nested
// inside that lattice's own At<> template.  Two such singletons from
// DIFFERENT lattices (BarrierStrengthLattice vs HwInstructionLattice) are
// unrelated types with no conversion between them — even though both are
// empty and both EBO-collapse identically inside a Graded<> carrier.
// This pins that a V-255 `Graded<Absolute, BarrierStrengthLattice::At<K>,
// P>` grade cannot be silently initialized from a foreign-lattice grade:
// the federation-cache row_hash (V-255) keys on the wrapper's grade type,
// so a cross-lattice grade swap would corrupt the cache key.
//
// Distinct from neg_barrier_strength_cross_lattice_mixing.cpp, which
// fails at a lattice OP argument; here the failure is a direct grade
// element_type assignment between two sibling lattices' singletons.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's no-viable-conversion
// rejection between two unrelated empty class types.

#include <crucible/algebra/lattices/BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    BarrierStrengthLattice::At<BarrierStrength::SeqCst>::element_type barrier_grade{};

    // Should FAIL: a HwInstruction At<> grade cannot be initialized from a
    // BarrierStrength At<> grade — they are unrelated empty class types.
    HwInstructionLattice::At<HwInstruction::Scalar>::element_type hw_grade = barrier_grade;

    return static_cast<int>(static_cast<HwInstruction>(hw_grade) == HwInstruction::Scalar);
}
