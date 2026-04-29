// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning CipherTier<TIER_A, T> to CipherTier<TIER_B, T>
// when TIER_A != TIER_B.
//
// Different Tier template arguments produce DIFFERENT class
// instantiations.  No converting assignment, no implicit conversion.
//
// Concrete bug-class this catches: a refactor adding a templated
// converting-assign operator on CipherTier would let a Cold-tier
// value silently flow into a Hot-tier slot — defeating the
// CRUCIBLE.md §L14 persistence-tier discipline at the assignment
// boundary that today catches storage-residency mismatches.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/CipherTier.h>

using namespace crucible::safety;

int main() {
    CipherTier<CipherTierTag_v::Hot,  int> hot_value{42};
    CipherTier<CipherTierTag_v::Cold, int> cold_value{7};

    // Should FAIL: hot_value and cold_value are DIFFERENT types.
    hot_value = cold_value;
    return hot_value.peek();
}
