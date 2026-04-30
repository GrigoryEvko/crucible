// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (Vendor) — pins constrained-extractor.
// vendor_value_t is constrained on `requires is_vendor_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsVendor.h>

int main() {
    using V = crucible::safety::extract::vendor_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
