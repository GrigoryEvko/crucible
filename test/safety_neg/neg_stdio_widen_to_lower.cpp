// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 1/2 for StdioPinned.  Mismatch class:
// widen-to-LOWER (unsound tighten).  Stdio is a capability-CEILING
// axis; widen() only goes UP.  `widen<NoStdio>()` on an InteractiveRead
// carrier — claiming no console I/O when the function blocks on stdin —
// is rejected by `requires (StdioLattice::leq(Tier, Higher))`:
// leq(InteractiveRead, NoStdio) is false.
//
// Pairs with neg_stdio_mint_wrong_arg.cpp.
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/Stdio.h>

int main() {
    using namespace crucible::safety;
    // FIXY-FOUND-090 #2245: construct via mint_stdio so the §XXI
    // inventory scanner counts this fixture toward HS14 — same lattice
    // failure, broader §XXI coverage.
    auto hi = mint_stdio<Stdio::InteractiveRead, int>(0);
    auto lo = hi.widen<Stdio::NoStdio>();  // FAIL: leq(InteractiveRead, NoStdio) == false
    (void)lo;
    return 0;
}
