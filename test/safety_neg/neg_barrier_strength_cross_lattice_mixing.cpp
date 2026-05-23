// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-252 BarrierStrengthLattice, mismatch class #1 of 2:
// CROSS-LATTICE ELEMENT MIXING at a lattice op.
//
// BarrierStrengthLattice::leq's signature requires two BarrierStrength
// values.  Passing a HwInstruction value (a SIBLING Agent 11 chain
// lattice, also uint8_t underlying) as the second argument is a
// strong-enum type-mismatch — `enum class` admits NO implicit cross-enum
// conversion.  This pins the TypeSafe disjointness the V-255
// BarrierGuarded gate depends on: a fence-strength tier must never be
// silently compared against a hw-instruction-capability tier (the two
// distinct Agent 11 HW axes).
//
// Mirrors test/safety_neg/neg_hw_instruction_cross_lattice_mixing.cpp.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    BarrierStrength barrier_val = BarrierStrength::SeqCst;
    HwInstruction   hw_val      = HwInstruction::Scalar;

    // Should FAIL: BarrierStrengthLattice::leq requires two BarrierStrength
    // values; passing a HwInstruction as the second argument is a type
    // mismatch (no cross-enum implicit conversion).
    return static_cast<int>(
        BarrierStrengthLattice::leq(barrier_val, hw_val));
}
