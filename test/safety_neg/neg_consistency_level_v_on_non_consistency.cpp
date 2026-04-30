// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D22 fixture — pins the constrained-extractor discipline.
// consistency_level_v is constrained on `requires is_consistency_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsConsistency.h>

int main() {
    auto t = crucible::safety::extract::consistency_level_v<int>;
    (void)t;
    return 0;
}
