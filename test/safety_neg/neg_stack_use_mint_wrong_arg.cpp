// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 2/2 for StackUsePinned.  Mismatch class:
// mint-wrong-arg.  `mint_stack_use<Tier, T>(args...)` gates on
// std::is_constructible_v<T, Args...>; NeedsTwo needs two ints, one is
// supplied — constraint fails, no overload matches.
//
// Pairs with neg_stack_use_widen_to_lower.cpp.
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/StackUse.h>

namespace { struct NeedsTwo { NeedsTwo(int, int) {} }; }

int main() {
    using namespace crucible::safety;
    auto bad = mint_stack_use<StackUse::ConstantFrame, NeedsTwo>(42);  // FAIL
    (void)bad;
    return 0;
}
