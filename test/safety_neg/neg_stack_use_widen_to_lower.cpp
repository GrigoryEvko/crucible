// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-242 HS14 fixture 1/2 for StackUsePinned.  Mismatch class:
// widen-to-LOWER (unsound tighten).  StackUse is a capability-CEILING
// axis; widen() only goes UP.  `widen<ConstantFrame>()` on an Unbounded
// carrier — claiming a tighter stack bound than reality — is rejected
// by `requires (StackUseLattice::leq(Tier, Higher))`: leq(Unbounded,
// ConstantFrame) is false.
//
// Pairs with neg_stack_use_mint_wrong_arg.cpp.
// Expected diagnostic: the constraint-failure family.

#include <crucible/safety/StackUse.h>

int main() {
    using namespace crucible::safety;
    StackUsePinned<StackUse::Unbounded, int> hi{0};
    auto lo = hi.widen<StackUse::ConstantFrame>();  // FAIL: leq(Unbounded, ConstantFrame) == false
    (void)lo;
    return 0;
}
