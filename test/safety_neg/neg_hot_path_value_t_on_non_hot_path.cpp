// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D25 fixture (HotPath) — pins constrained-extractor.
// hot_path_value_t is constrained on `requires is_hot_path_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsHotPath.h>

int main() {
    using V = crucible::safety::extract::hot_path_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
