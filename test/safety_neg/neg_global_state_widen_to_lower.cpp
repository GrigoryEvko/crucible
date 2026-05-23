// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 1/2 for GlobalStatePinned.  Mismatch class:
// widen-to-LOWER (unsound tighten).  GlobalState is a capability-CEILING
// axis; widen() only goes UP.  `widen<Stateless>()` on an
// InitOrderHazard carrier — claiming no global interaction when there
// is an init-order hazard — is rejected by `requires (
// GlobalStateLattice::leq(Tier, Higher))`: leq(InitOrderHazard,
// Stateless) is false.
//
// Pairs with neg_global_state_mint_wrong_arg.cpp.
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/GlobalState.h>

int main() {
    using namespace crucible::safety;
    GlobalStatePinned<GlobalState::InitOrderHazard, int> hi{0};
    auto lo = hi.widen<GlobalState::Stateless>();  // FAIL: leq(InitOrderHazard, Stateless) == false
    (void)lo;
    return 0;
}
