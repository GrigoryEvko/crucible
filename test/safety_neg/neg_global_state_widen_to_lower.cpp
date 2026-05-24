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
    // FIXY-FOUND-090 #2245: construct via mint_global_state so the §XXI
    // inventory scanner counts this fixture toward HS14 — same lattice
    // failure, broader §XXI coverage.
    auto hi = mint_global_state<GlobalState::InitOrderHazard, int>(0);
    auto lo = hi.widen<GlobalState::Stateless>();  // FAIL: leq(InitOrderHazard, Stateless) == false
    (void)lo;
    return 0;
}
