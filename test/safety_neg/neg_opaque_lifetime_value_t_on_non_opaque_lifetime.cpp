// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D23 fixture — pins the constrained-extractor discipline.
// opaque_lifetime_value_t is constrained on
// `requires is_opaque_lifetime_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsOpaqueLifetime.h>

int main() {
    using V = crucible::safety::extract::opaque_lifetime_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
