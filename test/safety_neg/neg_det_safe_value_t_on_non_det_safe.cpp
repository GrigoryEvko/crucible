// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D24 fixture — pins the constrained-extractor discipline.
// det_safe_value_t is constrained on `requires is_det_safe_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsDetSafe.h>

int main() {
    using V = crucible::safety::extract::det_safe_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
