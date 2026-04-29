// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a VendorBackend value to HotPathLattice::leq
// (or any cross-lattice mixing).
//
// Pins the structural disjointness of the TWELVE chain-or-partial-
// order lattice enums (Tolerance / Consistency / Lifetime / DetSafe /
// HotPath / Wait / MemOrder / Progress / AllocClass / CipherTier /
// ResidencyHeat / VendorBackend).  Each lattice carries its OWN
// strong scoped enum; cross-lattice mixing must be rejected at
// type level — even though all share uint8_t underlying type.
//
// SPECIAL-CASE FOR VENDOR: VendorBackend has a NON-DENSE underlying
// integer mapping (0/1/2/3/4/5/6/255 for None/CPU/NV/AMD/TPU/TRN/CER/
// Portable) chosen so that the spec's Portable=255 sentinel survives.
// A naive type-erased "shared Tier : uint8_t" refactor would mix
// VendorBackend's 255 with HotPathTier_v's 0/1/2 chain, producing
// nonsense at the leq comparison.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/HotPathLattice.h>
#include <crucible/algebra/lattices/VendorLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    VendorBackend vendor_val   = VendorBackend::NV;
    HotPathTier   hot_path_val = HotPathTier::Hot;

    // Should FAIL: HotPathLattice::leq's signature requires two
    // HotPathTier values; passing a VendorBackend as the second
    // argument is a type-mismatch.
    return static_cast<int>(
        HotPathLattice::leq(hot_path_val, vendor_val));
}
