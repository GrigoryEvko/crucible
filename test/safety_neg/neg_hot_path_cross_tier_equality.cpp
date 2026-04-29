// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing HotPath<TIER_A, T> with HotPath<TIER_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Tier, T) instantiation has its OWN
// friend taking two HotPath<Tier, T>&.  Cross-tier comparison
// fails to find a viable operator==.
//
// Concrete bug-class this catches: a refactor that introduced a
// template friend operator==(HotPath<...,A>, HotPath<...,B>) would
// silently let hot-path-tier mismatches at the comparison surface
// escape detection.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/HotPath.h>

using namespace crucible::safety;

int main() {
    HotPath<HotPathTier_v::Hot,  int> hot_value{42};
    HotPath<HotPathTier_v::Cold, int> cold_value{42};

    // Should FAIL: operator== for HotPath<Hot, int> takes two
    // HotPath<Hot, int>&; cold_value is HotPath<Cold, int>, no
    // implicit conversion.
    return static_cast<int>(hot_value == cold_value);
}
