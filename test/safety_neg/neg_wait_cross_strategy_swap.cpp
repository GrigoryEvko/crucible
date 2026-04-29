// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing Wait<STRATEGY_A, T> with Wait<STRATEGY_B, T>
// when STRATEGY_A != STRATEGY_B.
//
// swap() takes a reference to the SAME class — a member taking
// `Wait<Strategy, T>&`.  Cross-strategy swap is rejected at overload
// resolution because the parameter types disagree.
//
// Concrete bug-class this catches: a refactor adding cross-strategy
// swap would let wait-discipline labels swap independently of value
// bytes — a label vs bytes disjointness that allows futex-derived
// bytes to flow through a SpinPause-typed slot.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/Wait.h>
#include <utility>

using namespace crucible::safety;

int main() {
    Wait<WaitStrategy_v::SpinPause, int> spin_value{42};
    Wait<WaitStrategy_v::Block,     int> block_value{7};

    // Should FAIL: Wait<SpinPause, int>::swap takes
    // Wait<SpinPause, int>&; block_value is a different type.
    spin_value.swap(block_value);

    // Free-function (ADL) swap reaches the same rejection.
    using std::swap;
    swap(spin_value, block_value);

    return spin_value.peek();
}
