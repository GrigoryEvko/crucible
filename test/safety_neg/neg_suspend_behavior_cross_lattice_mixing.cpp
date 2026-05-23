// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-181 SuspendBehaviorLattice, mismatch class #1 of 2:
// CROSS-LATTICE ELEMENT MIXING at a lattice op.
//
// SuspendBehaviorLattice::leq's signature requires two SuspendBehavior
// values.  Passing a DetSafeTier value (the SIBLING Agent-6 clock-source
// axis, also uint8_t underlying) as the second argument is a strong-enum
// type-mismatch — `enum class` admits NO implicit cross-enum conversion.
// This pins the TypeSafe disjointness the V-184 ClockSourceLattice
// composition depends on: the suspend axis and the determinism axis
// COMPOSE via ProductLattice (wrapper-nesting), never via a single
// lattice op.  A suspend-behavior value must never be silently compared
// against a determinism tier.
//
// Mirrors test/safety_neg/neg_memory_scope_cross_lattice_mixing.cpp.
//
// Expected diagnostic: no match for / cannot convert / no matching
// function / invalid operands.

#include <crucible/algebra/lattices/DetSafeLattice.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    SuspendBehavior suspend_val = SuspendBehavior::KeepsTicking;
    DetSafeTier     det_val     = DetSafeTier::Pure;

    // Should FAIL: SuspendBehaviorLattice::leq requires two SuspendBehavior
    // values; passing a DetSafeTier as the second argument is a type
    // mismatch (no cross-enum implicit conversion).
    return static_cast<int>(
        SuspendBehaviorLattice::leq(suspend_val, det_val));
}
