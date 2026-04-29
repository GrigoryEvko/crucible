// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a CipherTierTag value to ProgressLattice::leq
// (or any cross-lattice mixing).
//
// Pins the structural disjointness of the TEN chain-lattice enums
// (Tolerance / Consistency / Lifetime / DetSafe / HotPath / Wait /
// MemOrder / Progress / AllocClass / CipherTier).  Each lattice
// carries its OWN strong scoped enum; cross-lattice mixing must be
// rejected at type level — even though all share uint8_t underlying
// type.
//
// A future refactor that collapsed the per-lattice enums into a
// shared `Tier : uint8_t` for "convenience" would silently allow
// (e.g.) CipherTierTag::Hot to flow where ProgressClass::Bounded
// was expected — semantically catastrophic.  This neg test would
// START passing (positive-compile) after such a regression.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/CipherTierLattice.h>
#include <crucible/algebra/lattices/ProgressLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    CipherTierTag tier_val     = CipherTierTag::Hot;
    ProgressClass progress_val = ProgressClass::Bounded;

    // Should FAIL: ProgressLattice::leq's signature requires two
    // ProgressClass values; passing a CipherTierTag as the second
    // argument is a type-mismatch.
    return static_cast<int>(
        ProgressLattice::leq(progress_val, tier_val));
}
