// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 2/2 for ControlFlowPinned.  Mismatch class:
// mint-wrong-arg (payload not constructible from the supplied args).
// `mint_control_flow<Tier, T>(args...)`'s requires-clause gates on
// std::is_constructible_v<T, Args...>.  NeedsTwo requires two ints; the
// mint is called with one — the constraint fails and no mint overload
// matches.
//
// Pairs with neg_control_flow_widen_to_lower.cpp (the lattice-direction
// mismatch class).
//
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/ControlFlow.h>

namespace { struct NeedsTwo { NeedsTwo(int, int) {} }; }

int main() {
    using namespace crucible::safety;
    auto bad = mint_control_flow<ControlFlow::Pure, NeedsTwo>(42);  // FAIL: NeedsTwo(int) ill-formed
    (void)bad;
    return 0;
}
