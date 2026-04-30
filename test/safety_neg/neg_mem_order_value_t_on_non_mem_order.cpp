// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D27 fixture (MemOrder) — pins constrained-extractor.
// mem_order_value_t is constrained on `requires is_mem_order_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsMemOrder.h>

int main() {
    using V = crucible::safety::extract::mem_order_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
