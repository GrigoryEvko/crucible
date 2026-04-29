// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning a Wait<STRATEGY_A, T> to a
// Wait<STRATEGY_B, T> when STRATEGY_A != STRATEGY_B.
//
// Different Strategy template arguments produce DIFFERENT class
// instantiations.  No converting assignment operator and no
// implicit conversion — the type system enforces per-strategy
// disjointness at the assignment surface.
//
// Concrete bug-class this catches: a refactor that added a
// templated converting-assign operator on Wait would let a
// Block-tier value silently flow into a SpinPause-tier slot —
// equivalent to the relax-to-stronger bug exploitable through
// the assignment surface.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/Wait.h>

using namespace crucible::safety;

int main() {
    Wait<WaitStrategy_v::SpinPause, int> spin_value{42};
    Wait<WaitStrategy_v::Block,     int> block_value{7};

    // Should FAIL: spin_value and block_value are DIFFERENT types.
    spin_value = block_value;
    return spin_value.peek();
}
