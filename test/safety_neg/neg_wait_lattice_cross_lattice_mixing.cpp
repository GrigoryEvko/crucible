// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a WaitStrategy value to HotPathLattice::leq
// (or any cross-lattice mixing).
//
// Pins the structural disjointness of the SIX chain-lattice enums
// (Tolerance / Consistency / Lifetime / DetSafe / HotPath / Wait).
// Each lattice carries its OWN strong scoped enum; cross-lattice
// mixing must be rejected at type level — even though all share
// uint8_t underlying type.
//
// A future refactor that collapsed the per-lattice enums into a
// shared `Tier : uint8_t` for "convenience" would silently allow
// (e.g.) WaitStrategy::SpinPause to flow where HotPathTier::Hot
// was expected — semantically catastrophic.  This neg test would
// START passing (positive-compile) after such a regression.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/HotPathLattice.h>
#include <crucible/algebra/lattices/WaitLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    HotPathTier  hot_path_val = HotPathTier::Hot;
    WaitStrategy wait_val     = WaitStrategy::SpinPause;

    // Should FAIL: HotPathLattice::leq's signature requires two
    // HotPathTier values; passing a WaitStrategy as the second
    // argument is a type-mismatch.
    return static_cast<int>(
        HotPathLattice::leq(hot_path_val, wait_val));
}
