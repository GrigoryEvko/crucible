// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing ResidencyHeat<TIER_A, T> with
// ResidencyHeat<TIER_B, T> via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Tier, T) instantiation has its OWN
// friend taking two ResidencyHeat<Tier, T>&.  Cross-tier
// comparison fails to find a viable operator==.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/ResidencyHeat.h>

using namespace crucible::safety;

int main() {
    ResidencyHeat<ResidencyHeatTag_v::Hot,  int> hot_value{42};
    ResidencyHeat<ResidencyHeatTag_v::Cold, int> cold_value{42};

    // Should FAIL: operator== for ResidencyHeat<Hot, int> takes
    // two ResidencyHeat<Hot, int>&; cold_value is
    // ResidencyHeat<Cold, int>.
    return static_cast<int>(hot_value == cold_value);
}
