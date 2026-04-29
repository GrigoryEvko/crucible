// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing Wait<STRATEGY_A, T> with Wait<STRATEGY_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Strategy, T) instantiation has its OWN
// friend taking two Wait<Strategy, T>&.  Cross-strategy comparison
// fails to find a viable operator==.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/Wait.h>

using namespace crucible::safety;

int main() {
    Wait<WaitStrategy_v::SpinPause, int> spin_value{42};
    Wait<WaitStrategy_v::Block,     int> block_value{42};

    // Should FAIL: operator== for Wait<SpinPause, int> takes two
    // Wait<SpinPause, int>&; block_value is Wait<Block, int>.
    return static_cast<int>(spin_value == block_value);
}
