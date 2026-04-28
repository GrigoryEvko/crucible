// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning a DetSafe<TIER_A, T> to a
// DetSafe<TIER_B, T> when TIER_A != TIER_B.
//
// Different Tier template arguments produce DIFFERENT class
// instantiations.  No converting assignment operator and no
// implicit conversion between them — the type system enforces
// per-tier disjointness at the assignment surface.
//
// Concrete bug-class this catches: a refactor that added a
// templated converting-assign operator on DetSafe would let a
// MonotonicClockRead-tier value silently flow into a Pure-tier
// slot — equivalent to the relax-to-stronger bug (this fixture's
// sister) but exploitable through the assignment surface instead
// of the relax<>() surface.  Pinning per-tier identity at BOTH
// surfaces is required for the 8th-axiom fence to hold.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/DetSafe.h>

using namespace crucible::safety;

int main() {
    DetSafe<DetSafeTier_v::Pure,               int> pure_value{42};
    DetSafe<DetSafeTier_v::MonotonicClockRead, int> mono_value{7};

    // Should FAIL: pure_value and mono_value are DIFFERENT types
    // — different template instantiations of DetSafe.  No
    // converting assignment exists.  Without this rejection, a
    // MonotonicClockRead-tier value could be assigned into a
    // Pure-tier slot and silently flow through the Cipher fence.
    pure_value = mono_value;
    return pure_value.peek();
}
