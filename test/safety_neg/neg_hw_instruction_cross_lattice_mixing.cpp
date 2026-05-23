// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-251 HwInstructionLattice, mismatch class #1 of 2:
// CROSS-LATTICE ELEMENT MIXING at a lattice op.
//
// HwInstructionLattice::leq's signature requires two HwInstruction
// values.  Passing a ControlFlow value (a SIBLING chain lattice that
// also uses uint8_t underlying type) as the second argument is a
// strong-enum type-mismatch — `enum class` admits NO implicit cross-enum
// conversion, even when the underlying integer types match.  This pins
// the TypeSafe disjointness the Mimic instruction-legalization gate
// depends on: an instruction-capability tier must never be silently
// compared against a control-flow-escape tier.
//
// Mirrors test/safety_neg/neg_vendor_lattice_cross_lattice_mixing.cpp.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/ControlFlowLattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    HwInstruction hw_val   = HwInstruction::Vectorizable;
    ControlFlow   cf_val   = ControlFlow::Pure;

    // Should FAIL: HwInstructionLattice::leq requires two HwInstruction
    // values; passing a ControlFlow as the second argument is a type
    // mismatch (no cross-enum implicit conversion).
    return static_cast<int>(
        HwInstructionLattice::leq(hw_val, cf_val));
}
