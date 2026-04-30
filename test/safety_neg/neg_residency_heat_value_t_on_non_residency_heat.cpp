// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (ResidencyHeat) — pins constrained-extractor.
// residency_heat_value_t is constrained on
// `requires is_residency_heat_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsResidencyHeat.h>

int main() {
    using V = crucible::safety::extract::residency_heat_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
