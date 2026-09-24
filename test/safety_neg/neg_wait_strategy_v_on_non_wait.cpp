// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D26 fixture (Wait) — pins constrained-extractor.
// wait_strategy_v is constrained on `requires is_wait_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsWait.h>

int main() {
    auto s = crucible::safety::extract::wait_strategy_v<int>;
    (void)s;
    return 0;
}
