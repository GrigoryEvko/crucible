// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing Consistency<LEVEL_A, T> with
// Consistency<LEVEL_B, T> via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Level, T) instantiation has its OWN
// friend taking two Consistency<Level, T>&.  Cross-level
// comparison fails to find a viable operator==.
//
// Concrete bug-class this catches: a refactor that introduced a
// template friend operator==(Consistency<...,A>, Consistency<...,B>)
// would silently let consistency-level mismatches at the
// comparison surface escape detection — every site doing
// `if (strong_committed == eventual_replica) ...` would compile
// and silently compare bytes across levels, hiding cases where
// the two values came from incompatible commit protocols.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/Consistency.h>

using namespace crucible::safety;

int main() {
    Consistency<Consistency_v::STRONG,        int> strong_value{42};
    Consistency<Consistency_v::CAUSAL_PREFIX, int> causal_value{42};

    // Should FAIL: operator== for Consistency<STRONG, int> takes
    // two Consistency<STRONG, int>&; causal_value is
    // Consistency<CAUSAL_PREFIX, int>, no implicit conversion.
    return static_cast<int>(strong_value == causal_value);
}
