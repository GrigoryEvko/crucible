// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 1/2 for ControlFlowPinned.  Mismatch class:
// widen-to-LOWER (unsound tighten).  ControlFlow is a capability-CEILING
// axis: widen() may only go UP the chain (over-approximating escape
// capability is conservative), so `widen<Pure>()` on a MaySignal carrier
// — claiming the value is SAFER than it is — must be rejected by the
// `requires (ControlFlowLattice::leq(Tier, Higher))` clause.  leq(
// MaySignal, Pure) is false, so no widen overload matches.
//
// Pairs with neg_control_flow_mint_wrong_arg.cpp (the constructibility
// mismatch class).
//
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/ControlFlow.h>

int main() {
    using namespace crucible::safety;
    ControlFlowPinned<ControlFlow::MaySignal, int> hi{0};
    auto lo = hi.widen<ControlFlow::Pure>();  // FAIL: leq(MaySignal, Pure) == false
    (void)lo;
    return 0;
}
