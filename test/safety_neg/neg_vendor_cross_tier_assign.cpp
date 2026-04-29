// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning Vendor<BACKEND_A, T> to Vendor<BACKEND_B, T>
// when BACKEND_A != BACKEND_B.
//
// Different Backend template arguments produce DIFFERENT class
// instantiations.  No converting assignment, no implicit conversion.
//
// Concrete bug-class this catches: a refactor adding a templated
// converting-assign operator on Vendor would let an NV-pinned
// kernel silently flow into an AMD-pinned slot — defeating MIMIC
// per-vendor backend identity at the assignment boundary.
//
// [GCC-WRAPPER-TEXT] — assignment-operator type-mismatch rejection.

#include <crucible/safety/Vendor.h>

using namespace crucible::safety;

int main() {
    Vendor<VendorBackend_v::NV,  int> nv_value{42};
    Vendor<VendorBackend_v::AMD, int> amd_value{7};

    // Should FAIL: nv_value and amd_value are DIFFERENT types.
    nv_value = amd_value;
    return nv_value.peek();
}
