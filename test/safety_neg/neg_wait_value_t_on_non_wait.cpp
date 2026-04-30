// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D26 fixture (Wait) — pins constrained-extractor.
// wait_value_t is constrained on `requires is_wait_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsWait.h>

int main() {
    using V = crucible::safety::extract::wait_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
