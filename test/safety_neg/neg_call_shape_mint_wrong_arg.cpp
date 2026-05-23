// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 2/2 for CallShapePinned.  Mismatch class:
// mint-wrong-arg.  `mint_call_shape<Tier, T>(args...)` gates on
// std::is_constructible_v<T, Args...>; NeedsTwo needs two ints, the
// mint supplies one — constraint fails, no overload matches.
//
// Pairs with neg_call_shape_widen_to_lower.cpp.
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/CallShape.h>

namespace { struct NeedsTwo { NeedsTwo(int, int) {} }; }

int main() {
    using namespace crucible::safety;
    auto bad = mint_call_shape<CallShape::Direct, NeedsTwo>(42);  // FAIL
    (void)bad;
    return 0;
}
