// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-251 HwInstructionLattice, mismatch class #2 of 2:
// At<T> SINGLETON GRADE DISJOINTNESS.
//
// Each lattice's `At<T>::element_type` is a DISTINCT empty struct nested
// inside that lattice's own At<> template.  Two such singletons from
// DIFFERENT lattices (HwInstructionLattice vs ControlFlowLattice) are
// unrelated types with no conversion between them — even though both are
// empty and both EBO-collapse identically inside a Graded<> carrier.
// This pins that a V-254 `Graded<Absolute, HwInstructionLattice::At<T>,
// P>` grade cannot be silently initialized from, or assigned to, a
// foreign-lattice grade: the federation-cache row_hash (V-254) keys on
// the wrapper's grade type, so a cross-lattice grade swap would corrupt
// the cache key.
//
// Distinct from neg_hw_instruction_cross_lattice_mixing.cpp, which fails
// at a lattice OP argument; here the failure is a direct grade
// element_type assignment between two sibling lattices' singletons.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's no-viable-conversion
// rejection between two unrelated empty class types.

#include <crucible/algebra/lattices/ControlFlowLattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    HwInstructionLattice::At<HwInstruction::Vectorizable>::element_type hw_grade{};

    // Should FAIL: a ControlFlow At<> grade cannot be initialized from an
    // HwInstruction At<> grade — they are unrelated empty class types.
    ControlFlowLattice::At<ControlFlow::Pure>::element_type cf_grade = hw_grade;

    return static_cast<int>(static_cast<ControlFlow>(cf_grade) == ControlFlow::Pure);
}
