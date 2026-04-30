// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (Crash) — pins constrained-extractor.
// crash_class_v is constrained on `requires is_crash_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsCrash.h>

int main() {
    auto c = crucible::safety::extract::crash_class_v<int>;
    (void)c;
    return 0;
}
