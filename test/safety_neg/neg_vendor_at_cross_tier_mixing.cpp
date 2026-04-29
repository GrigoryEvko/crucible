// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a VendorLattice::At<BACKEND_B>::element_type
// to a function expecting VendorLattice::At<BACKEND_A>::element_type.
//
// Per-At<T> nested-struct-identity at the LATTICE substrate.
// Mirrors the per-At<T> mixing fixtures shipped for the eight
// sister chain lattices.  A future refactor extracting a shared
// singleton_carrier<VendorBackend> alias above At<T> would silently
// allow NV-tier and AMD-tier values to interconvert at the lattice
// level, bypassing the per-vendor backend discipline via Graded's
// compose / weaken paths.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/VendorLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    VendorLattice::At<VendorBackend::NV>::element_type  nv_elt{};
    VendorLattice::At<VendorBackend::AMD>::element_type amd_elt{};

    // Should FAIL: At<NV>::leq expects two At<NV>::element_type
    // arguments; amd_elt is At<AMD>::element_type.
    return static_cast<int>(
        VendorLattice::At<VendorBackend::NV>::leq(nv_elt, amd_elt));
}
