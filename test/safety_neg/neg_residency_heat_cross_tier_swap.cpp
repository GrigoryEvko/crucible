// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing ResidencyHeat<TIER_A, T> with
// ResidencyHeat<TIER_B, T> when TIER_A != TIER_B.
//
// swap() takes a reference to the SAME class — a member taking
// `ResidencyHeat<Tier, T>&`.  Cross-tier swap is rejected at
// overload resolution.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/ResidencyHeat.h>
#include <utility>

using namespace crucible::safety;

int main() {
    ResidencyHeat<ResidencyHeatTag_v::Hot,  int> hot_value{42};
    ResidencyHeat<ResidencyHeatTag_v::Cold, int> cold_value{7};

    // Should FAIL: ResidencyHeat<Hot, int>::swap takes
    // ResidencyHeat<Hot, int>&; cold_value is a different type.
    hot_value.swap(cold_value);

    using std::swap;
    swap(hot_value, cold_value);

    return hot_value.peek();
}
