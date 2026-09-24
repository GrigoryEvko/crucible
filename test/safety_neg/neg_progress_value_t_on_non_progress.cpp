// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D28 fixture (Progress) — pins constrained-extractor.
// progress_value_t is constrained on `requires is_progress_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsProgress.h>

int main() {
    using V = crucible::safety::extract::progress_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
