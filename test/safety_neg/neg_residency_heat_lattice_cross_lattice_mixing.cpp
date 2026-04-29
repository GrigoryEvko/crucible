// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a ResidencyHeatTag value to
// CipherTierLattice::leq (or any cross-lattice mixing).
//
// Pins the structural disjointness of the ELEVEN chain-lattice
// enums (Tolerance / Consistency / Lifetime / DetSafe / HotPath /
// Wait / MemOrder / Progress / AllocClass / CipherTier /
// ResidencyHeat).  Each lattice carries its OWN strong scoped
// enum; cross-lattice mixing must be rejected at type level —
// even though all share uint8_t underlying type AND the three
// 3-tier chains (HotPath / CipherTier / ResidencyHeat) all share
// the structurally-identical Hot-at-top shape.
//
// THIS IS THE LOAD-BEARING DISJOINTNESS ASSERTION for the 28_04
// §4.7 universal-vocabulary claim.  Three structurally-identical
// 3-tier chains MUST stay type-disjoint even though their layout
// is byte-equivalent — otherwise the dispatcher's reflection-
// driven row composition would conflate execution-budget,
// storage-residency, AND cache-residency-heat at the type level.
//
// A future refactor that collapsed the per-lattice enums into a
// shared `Tier : uint8_t` for "convenience" would silently allow
// (e.g.) ResidencyHeatTag::Hot to flow where CipherTierTag::Warm
// was expected — semantically catastrophic.  This neg test would
// START passing (positive-compile) after such a regression.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/CipherTierLattice.h>
#include <crucible/algebra/lattices/ResidencyHeatLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    ResidencyHeatTag heat_val   = ResidencyHeatTag::Hot;
    CipherTierTag    cipher_val = CipherTierTag::Warm;

    // Should FAIL: CipherTierLattice::leq's signature requires two
    // CipherTierTag values; passing a ResidencyHeatTag as the
    // second argument is a type-mismatch.
    return static_cast<int>(
        CipherTierLattice::leq(cipher_val, heat_val));
}
