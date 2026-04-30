// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D22 fixture — pins the constrained-extractor discipline.
// consistency_value_t is constrained on `requires is_consistency_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsConsistency.h>

int main() {
    using V = crucible::safety::extract::consistency_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
