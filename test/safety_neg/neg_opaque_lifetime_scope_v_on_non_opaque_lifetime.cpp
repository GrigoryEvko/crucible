// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D23 fixture — pins the constrained-extractor discipline.
// opaque_lifetime_scope_v is constrained on
// `requires is_opaque_lifetime_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsOpaqueLifetime.h>

int main() {
    auto t = crucible::safety::extract::opaque_lifetime_scope_v<int>;
    (void)t;
    return 0;
}
