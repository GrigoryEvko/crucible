// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a MemOrderTag value to WaitLattice::leq
// (or any cross-lattice mixing).
//
// Pins the structural disjointness of the SEVEN chain-lattice enums
// (Tolerance / Consistency / Lifetime / DetSafe / HotPath / Wait /
// MemOrder).  Each lattice carries its OWN strong scoped enum;
// cross-lattice mixing must be rejected at type level — even
// though all share uint8_t underlying type.
//
// A future refactor that collapsed the per-lattice enums into a
// shared `Tier : uint8_t` for "convenience" would silently allow
// (e.g.) MemOrderTag::Relaxed to flow where WaitStrategy::SpinPause
// was expected — semantically catastrophic.  This neg test would
// START passing (positive-compile) after such a regression.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/MemOrderLattice.h>
#include <crucible/algebra/lattices/WaitLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    MemOrderTag  mem_order_val = MemOrderTag::Relaxed;
    WaitStrategy wait_val      = WaitStrategy::SpinPause;

    // Should FAIL: WaitLattice::leq's signature requires two
    // WaitStrategy values; passing a MemOrderTag as the second
    // argument is a type-mismatch.
    return static_cast<int>(
        WaitLattice::leq(wait_val, mem_order_val));
}
