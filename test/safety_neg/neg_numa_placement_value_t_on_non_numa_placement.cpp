// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (NumaPlacement, third product wrapper) — pins
// constrained-extractor.  numa_placement_value_t is constrained on
// `requires is_numa_placement_v<T>`.
//
// Single fixture (vs two for NTTP wrappers) — same product-wrapper
// rationale: no compile-time tag exists.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsNumaPlacement.h>

int main() {
    using V = crucible::safety::extract::numa_placement_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
