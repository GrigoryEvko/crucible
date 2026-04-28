// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing NumericalTier<TIER_A, T> with
// NumericalTier<TIER_B, T> via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (T_at, T) instantiation has its OWN
// friend taking two NumericalTier<T_at, T>&.  Cross-tier
// comparison fails to find a viable operator== — the LHS's friend
// expects the RHS to be the same instantiation, which it isn't.
//
// Concrete bug-class this catches: a refactor that introduced a
// template friend operator==(NumericalTier<A>, NumericalTier<B>)
// (e.g. for "convenience cross-tier comparison" via a converting
// rule) would silently let recipe-tier mismatches at the
// comparison surface escape detection — every site doing
// `if (bitexact_result == fp16_replica) ...` would compile and
// silently compare bytes across tiers, hiding cases where the two
// values came from incompatible recipes.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/NumericalTier.h>

using namespace crucible::safety;

int main() {
    NumericalTier<Tolerance::BITEXACT, int>  bx{42};
    NumericalTier<Tolerance::ULP_FP16, int>  fp16{42};

    // Should FAIL: operator== for NumericalTier<BITEXACT, int>
    // takes two NumericalTier<BITEXACT, int>&; fp16 is
    // NumericalTier<ULP_FP16, int>, no implicit conversion to
    // the LHS's instantiation.  Symmetric for the RHS friend.
    return static_cast<int>(bx == fp16);
}
