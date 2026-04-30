// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D27 fixture (MemOrder) — pins constrained-extractor.
// mem_order_tag_v is constrained on `requires is_mem_order_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsMemOrder.h>

int main() {
    auto t = crucible::safety::extract::mem_order_tag_v<int>;
    (void)t;
    return 0;
}
