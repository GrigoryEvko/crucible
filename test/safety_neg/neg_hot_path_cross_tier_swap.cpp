// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing HotPath<TIER_A, T> with HotPath<TIER_B, T>
// when TIER_A != TIER_B.
//
// swap() takes a reference to the SAME class — a member taking
// `HotPath<Tier, T>&`.  Cross-tier swap is rejected at overload
// resolution because the parameter types disagree.
//
// Concrete bug-class this catches: a refactor adding cross-tier
// swap (perhaps for SoA gather where multiple HotPath tiers
// cohabit one buffer) would let hot-path budget labels swap
// independently of value bytes — a tier-label vs value-bytes
// disjointness that allows cold-context bytes to flow through a
// Hot-typed slot.  Per-tier identity at the swap surface is the
// third leg (alongside relax<> and operator=) the hot-path
// dispatcher gate stands on.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/HotPath.h>
#include <utility>

using namespace crucible::safety;

int main() {
    HotPath<HotPathTier_v::Hot,  int> hot_value{42};
    HotPath<HotPathTier_v::Cold, int> cold_value{7};

    // Should FAIL: HotPath<Hot, int>::swap takes
    // HotPath<Hot, int>&; cold_value is a different type.
    hot_value.swap(cold_value);

    // Free-function (ADL) swap reaches the same rejection.
    using std::swap;
    swap(hot_value, cold_value);

    return hot_value.peek();
}
