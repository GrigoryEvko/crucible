// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-182 PinningRequirementLattice, mismatch class #1 of 2:
// CROSS-LATTICE ELEMENT MIXING at a lattice op.
//
// PinningRequirementLattice::leq's signature requires two
// PinningRequirement values.  Passing a SuspendBehavior value (the
// SIBLING Agent-6 clock-source axis, also uint8_t underlying) as the
// second argument is a strong-enum type-mismatch — `enum class` admits
// NO implicit cross-enum conversion.  This pins the TypeSafe
// disjointness the V-184 ClockSourceLattice composition depends on: the
// pinning axis and the suspend axis COMPOSE via ProductLattice (wrapper-
// nesting), never via a single lattice op.  A pinning-requirement value
// must never be silently compared against a suspend-behavior value.
//
// Mirrors test/safety_neg/neg_suspend_behavior_cross_lattice_mixing.cpp.
//
// Expected diagnostic: no match for / cannot convert / no matching
// function / invalid operands.

#include <crucible/algebra/lattices/PinningRequirementLattice.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    PinningRequirement pin_val     = PinningRequirement::PerCore;
    SuspendBehavior    suspend_val = SuspendBehavior::KeepsTicking;

    // Should FAIL: PinningRequirementLattice::leq requires two
    // PinningRequirement values; passing a SuspendBehavior as the second
    // argument is a type mismatch (no cross-enum implicit conversion).
    return static_cast<int>(
        PinningRequirementLattice::leq(pin_val, suspend_val));
}
