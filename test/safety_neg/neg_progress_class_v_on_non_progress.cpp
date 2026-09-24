// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D28 fixture (Progress) — pins constrained-extractor.
// progress_class_v is constrained on `requires is_progress_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsProgress.h>

int main() {
    auto c = crucible::safety::extract::progress_class_v<int>;
    (void)c;
    return 0;
}
