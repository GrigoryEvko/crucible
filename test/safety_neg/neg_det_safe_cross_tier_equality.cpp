// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing DetSafe<TIER_A, T> with DetSafe<TIER_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Tier, T) instantiation has its OWN
// friend taking two DetSafe<Tier, T>&.  Cross-tier comparison
// fails to find a viable operator==.
//
// Concrete bug-class this catches: a refactor that introduced a
// template friend operator==(DetSafe<...,A>, DetSafe<...,B>) would
// silently let determinism-tier mismatches at the comparison
// surface escape detection.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/DetSafe.h>

using namespace crucible::safety;

int main() {
    DetSafe<DetSafeTier_v::Pure,               int> pure_value{42};
    DetSafe<DetSafeTier_v::MonotonicClockRead, int> mono_value{42};

    // Should FAIL: operator== for DetSafe<Pure, int> takes two
    // DetSafe<Pure, int>&; mono_value is DetSafe<MonotonicClockRead,
    // int>, no implicit conversion.
    return static_cast<int>(pure_value == mono_value);
}
