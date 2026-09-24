// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 1/2 for CallShapePinned.  Mismatch class:
// widen-to-LOWER (unsound tighten).  CallShape is a capability-CEILING
// axis; widen() only goes UP the chain.  `widen<Direct>()` on an
// Unbounded carrier — claiming a more-analyzable shape than reality —
// is rejected by `requires (CallShapeLattice::leq(Tier, Higher))`:
// leq(Unbounded, Direct) is false.
//
// Pairs with neg_call_shape_mint_wrong_arg.cpp.
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/CallShape.h>

int main() {
    using namespace crucible::safety;
    // FIXY-FOUND-090 #2245: construct via mint_call_shape so the §XXI
    // inventory scanner counts this fixture toward HS14 — same lattice
    // failure, broader §XXI coverage.
    auto hi = mint_call_shape<CallShape::Unbounded, int>(0);
    auto lo = hi.widen<CallShape::Direct>();  // FAIL: leq(Unbounded, Direct) == false
    (void)lo;
    return 0;
}
