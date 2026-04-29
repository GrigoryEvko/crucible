// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing Vendor<BACKEND_A, T> with
// Vendor<BACKEND_B, T> when BACKEND_A != BACKEND_B.
//
// swap() takes a reference to the SAME class — a member taking
// `Vendor<Backend, T>&`.  Cross-backend swap is rejected at
// overload resolution.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/Vendor.h>
#include <utility>

using namespace crucible::safety;

int main() {
    Vendor<VendorBackend_v::NV,  int> nv_value{42};
    Vendor<VendorBackend_v::AMD, int> amd_value{7};

    // Should FAIL: Vendor<NV, int>::swap takes
    // Vendor<NV, int>&; amd_value is a different type.
    nv_value.swap(amd_value);

    using std::swap;
    swap(nv_value, amd_value);

    return nv_value.peek();
}
