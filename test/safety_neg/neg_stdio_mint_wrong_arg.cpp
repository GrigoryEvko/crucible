// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 2/2 for StdioPinned.  Mismatch class:
// mint-wrong-arg.  `mint_stdio<Tier, T>(args...)` gates on
// std::is_constructible_v<T, Args...>; NeedsTwo needs two ints, one is
// supplied — constraint fails, no overload matches.
//
// Pairs with neg_stdio_widen_to_lower.cpp.
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/Stdio.h>

namespace { struct NeedsTwo { NeedsTwo(int, int) {} }; }

int main() {
    using namespace crucible::safety;
    auto bad = mint_stdio<Stdio::NoStdio, NeedsTwo>(42);  // FAIL
    (void)bad;
    return 0;
}
