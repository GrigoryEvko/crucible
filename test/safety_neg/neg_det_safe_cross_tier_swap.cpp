// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing DetSafe<TIER_A, T> with DetSafe<TIER_B, T>
// when TIER_A != TIER_B.
//
// swap() takes a reference to the SAME class — a member taking
// `DetSafe<Tier, T>&`.  Cross-tier swap is rejected at overload
// resolution because the parameter types disagree.
//
// Concrete bug-class this catches: a refactor adding cross-tier
// swap (perhaps for SoA gather where multiple DetSafe tiers
// cohabit one buffer) would let determinism-tier labels swap
// independently of value bytes — a tier-label vs value-bytes
// disjointness that allows clock-derived bytes to flow through a
// Pure-typed slot.  Per-tier identity at the swap surface is the
// third leg (alongside relax<> and operator=) the 8th-axiom fence
// stands on.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/DetSafe.h>
#include <utility>

using namespace crucible::safety;

int main() {
    DetSafe<DetSafeTier_v::Pure,               int> pure_value{42};
    DetSafe<DetSafeTier_v::MonotonicClockRead, int> mono_value{7};

    // Should FAIL: DetSafe<Pure, int>::swap takes
    // DetSafe<Pure, int>&; mono_value is a different type.
    pure_value.swap(mono_value);

    // Free-function (ADL) swap reaches the same rejection.
    using std::swap;
    swap(pure_value, mono_value);

    return pure_value.peek();
}
