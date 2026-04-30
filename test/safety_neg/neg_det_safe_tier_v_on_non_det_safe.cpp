// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D24 fixture — pins the constrained-extractor discipline.
// det_safe_tier_v is constrained on `requires is_det_safe_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsDetSafe.h>

int main() {
    auto t = crucible::safety::extract::det_safe_tier_v<int>;
    (void)t;
    return 0;
}
