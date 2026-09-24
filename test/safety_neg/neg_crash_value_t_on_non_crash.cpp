// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (Crash) — pins constrained-extractor.
// crash_value_t is constrained on `requires is_crash_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsCrash.h>

int main() {
    using V = crucible::safety::extract::crash_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
