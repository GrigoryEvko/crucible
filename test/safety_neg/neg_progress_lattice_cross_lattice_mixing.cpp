// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a ProgressClass value to MemOrderLattice::leq
// (or any cross-lattice mixing).
//
// Pins the structural disjointness of the EIGHT chain-lattice enums
// (Tolerance / Consistency / Lifetime / DetSafe / HotPath / Wait /
// MemOrder / Progress).  Each lattice carries its OWN strong scoped
// enum; cross-lattice mixing must be rejected at type level — even
// though all share uint8_t underlying type.
//
// A future refactor that collapsed the per-lattice enums into a
// shared `Tier : uint8_t` for "convenience" would silently allow
// (e.g.) ProgressClass::Bounded to flow where MemOrderTag::AcqRel
// was expected — semantically catastrophic.  This neg test would
// START passing (positive-compile) after such a regression.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/MemOrderLattice.h>
#include <crucible/algebra/lattices/ProgressLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    ProgressClass progress_val   = ProgressClass::Bounded;
    MemOrderTag   mem_order_val  = MemOrderTag::AcqRel;

    // Should FAIL: MemOrderLattice::leq's signature requires two
    // MemOrderTag values; passing a ProgressClass as the second
    // argument is a type-mismatch.
    return static_cast<int>(
        MemOrderLattice::leq(mem_order_val, progress_val));
}
