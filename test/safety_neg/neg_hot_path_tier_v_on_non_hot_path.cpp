// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D25 fixture (HotPath) — pins constrained-extractor.
// hot_path_tier_v is constrained on `requires is_hot_path_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsHotPath.h>

int main() {
    auto t = crucible::safety::extract::hot_path_tier_v<int>;
    (void)t;
    return 0;
}
