// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D29 fixture (AllocClass) — pins constrained-extractor.
// alloc_class_tag_v is constrained on `requires is_alloc_class_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsAllocClass.h>

int main() {
    auto t = crucible::safety::extract::alloc_class_tag_v<int>;
    (void)t;
    return 0;
}
