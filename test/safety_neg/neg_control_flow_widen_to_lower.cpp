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
    // FIXY-FOUND-090 #2245: construct via mint_control_flow so the §XXI
    // inventory scanner counts this fixture toward HS14 — same lattice
    // failure, broader §XXI coverage.
    auto hi = mint_control_flow<ControlFlow::MaySignal, int>(0);
    auto lo = hi.widen<ControlFlow::Pure>();  // FAIL: leq(MaySignal, Pure) == false
    (void)lo;
    return 0;
}
