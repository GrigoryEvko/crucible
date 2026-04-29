// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing an AllocClassTag value to ProgressLattice::leq
// (or any cross-lattice mixing).
//
// Pins the structural disjointness of the NINE chain-lattice enums
// (Tolerance / Consistency / Lifetime / DetSafe / HotPath / Wait /
// MemOrder / Progress / AllocClass).  Each lattice carries its OWN
// strong scoped enum; cross-lattice mixing must be rejected at type
// level — even though all share uint8_t underlying type.
//
// A future refactor that collapsed the per-lattice enums into a
// shared `Tier : uint8_t` for "convenience" would silently allow
// (e.g.) AllocClassTag::Stack to flow where ProgressClass::Bounded
// was expected — semantically catastrophic.  This neg test would
// START passing (positive-compile) after such a regression.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/AllocClassLattice.h>
#include <crucible/algebra/lattices/ProgressLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    AllocClassTag alloc_class_val = AllocClassTag::Stack;
    ProgressClass progress_val    = ProgressClass::Bounded;

    // Should FAIL: ProgressLattice::leq's signature requires two
    // ProgressClass values; passing an AllocClassTag as the second
    // argument is a type-mismatch.
    return static_cast<int>(
        ProgressLattice::leq(progress_val, alloc_class_val));
}
