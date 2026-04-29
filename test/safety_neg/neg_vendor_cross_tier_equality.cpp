// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing Vendor<BACKEND_A, T> with Vendor<BACKEND_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Backend, T) instantiation has its OWN
// friend taking two Vendor<Backend, T>&.  Cross-backend comparison
// fails to find a viable operator==.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/Vendor.h>

using namespace crucible::safety;

int main() {
    Vendor<VendorBackend_v::NV,  int> nv_value{42};
    Vendor<VendorBackend_v::AMD, int> amd_value{42};

    // Should FAIL: operator== for Vendor<NV, int> takes two
    // Vendor<NV, int>&; amd_value is Vendor<AMD, int>.
    return static_cast<int>(nv_value == amd_value);
}
