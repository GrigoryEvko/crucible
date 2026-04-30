// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D29 fixture (AllocClass) — pins constrained-extractor.
// alloc_class_value_t is constrained on `requires is_alloc_class_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsAllocClass.h>

int main() {
    using V = crucible::safety::extract::alloc_class_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
