// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing Consistency<LEVEL_A, T> with
// Consistency<LEVEL_B, T> when LEVEL_A != LEVEL_B.
//
// swap() takes a reference to the SAME class — a member taking
// `Consistency<Level, T>&`.  Cross-level swap is rejected at
// overload resolution because the parameter types disagree.
//
// Concrete bug-class this catches: a refactor that added cross-
// level swap (perhaps for SoA gather where multiple consistency
// levels cohabit one buffer) would silently let consistency-level
// labels swap while the underlying bytes do not move correlated
// — a tier-label vs value-bytes disjointness that breaks every
// downstream Forge Phase K consumer.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/Consistency.h>
#include <utility>

using namespace crucible::safety;

int main() {
    Consistency<Consistency_v::STRONG,        int> strong_value{42};
    Consistency<Consistency_v::CAUSAL_PREFIX, int> causal_value{7};

    // Should FAIL: Consistency<STRONG, int>::swap takes
    // Consistency<STRONG, int>&; causal_value is a different
    // type (different Level template arg).
    strong_value.swap(causal_value);

    // Free-function (ADL) swap reaches the same rejection.
    using std::swap;
    swap(strong_value, causal_value);

    return strong_value.peek();
}
