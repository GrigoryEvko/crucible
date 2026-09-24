// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (Vendor) — pins constrained-extractor.
// vendor_backend_v is constrained on `requires is_vendor_v<T>`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsVendor.h>

int main() {
    auto b = crucible::safety::extract::vendor_backend_v<int>;
    (void)b;
    return 0;
}
