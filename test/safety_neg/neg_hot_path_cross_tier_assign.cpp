// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning a HotPath<TIER_A, T> to a
// HotPath<TIER_B, T> when TIER_A != TIER_B.
//
// Different Tier template arguments produce DIFFERENT class
// instantiations.  No converting assignment operator and no
// implicit conversion between them — the type system enforces
// per-tier disjointness at the assignment surface.
//
// Concrete bug-class this catches: a refactor that added a
// templated converting-assign operator on HotPath would let a
// Cold-tier value silently flow into a Hot-tier slot — equivalent
// to the relax-to-stronger bug (this fixture's sister) but
// exploitable through the assignment surface instead of the
// relax<>() surface.  Pinning per-tier identity at BOTH surfaces
// is required for the hot-path discipline to hold.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/HotPath.h>

using namespace crucible::safety;

int main() {
    HotPath<HotPathTier_v::Hot,  int> hot_value{42};
    HotPath<HotPathTier_v::Cold, int> cold_value{7};

    // Should FAIL: hot_value and cold_value are DIFFERENT types
    // — different template instantiations of HotPath.  No
    // converting assignment exists.  Without this rejection, a
    // Cold-tier value could be assigned into a Hot-tier slot and
    // silently flow through the hot-path admission gate.
    hot_value = cold_value;
    return hot_value.peek();
}
