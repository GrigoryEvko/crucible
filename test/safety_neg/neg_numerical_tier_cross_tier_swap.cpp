// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing NumericalTier<TIER_A, T> with
// NumericalTier<TIER_B, T> when TIER_A != TIER_B.
//
// swap() takes a reference to the SAME class — a member taking
// `NumericalTier<T_at, T>&`.  Cross-tier swap is rejected at
// overload resolution because the parameter types disagree.
//
// Concrete bug-class this catches: a refactor that added a
// cross-tier swap (perhaps for SoA gather where multiple recipe
// tiers cohabit one buffer) would silently let recipe-tier
// labels swap while the underlying bytes do not move correlated
// — the result is a tier-label vs value-bytes disjointness that
// breaks every downstream consumer.  Pinning the swap signature
// to same-tier is the structural fence.
//
// Standard-library `std::swap` reaches the same conclusion: it
// requires a single `T&` for both operands; cross-instantiation
// disagreement at resolution time fails at the substitution.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/NumericalTier.h>
#include <utility>

using namespace crucible::safety;

int main() {
    NumericalTier<Tolerance::BITEXACT, int>  bx{42};
    NumericalTier<Tolerance::ULP_FP16, int>  fp16{7};

    // Should FAIL: NumericalTier<BITEXACT, int>::swap takes
    // NumericalTier<BITEXACT, int>&; fp16 is a different type
    // (different T_at template arg).  No converting swap exists.
    bx.swap(fp16);

    // Free-function (ADL) swap reaches the same rejection — the
    // friend `swap(NumericalTier&, NumericalTier&)` requires both
    // operands to have the same template-instantiation identity.
    using std::swap;
    swap(bx, fp16);

    return bx.peek();
}
