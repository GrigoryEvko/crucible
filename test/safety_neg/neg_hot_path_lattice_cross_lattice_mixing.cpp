// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a HotPathTier value to DetSafeLattice::leq
// (or any cross-lattice mixing across the five chain-lattice
// families: Tolerance / Consistency / Lifetime / DetSafe / HotPath).
//
// Symmetric to neg_consistency_cross_lattice_mixing /
// neg_tolerance_cross_lattice_mixing / neg_lifetime_tolerance_
// cross_lattice_mixing / neg_det_safe_lattice_cross_lattice_mixing.
// Pins the structural disjointness of the FIVE chain-lattice enums.
// Each lattice carries its OWN strong scoped enum, and cross-
// lattice mixing must be rejected at type level — even though all
// share the same underlying type (uint8_t).
//
// A future refactor that collapsed the per-lattice enums into a
// shared `Tier : uint8_t` for "convenience" would silently allow
// (e.g.) HotPathTier::Hot to flow where DetSafeTier::Pure was
// expected — semantically catastrophic even though both are uint8_t.
// This neg test would START passing (positive-compile) after such
// a regression.
//
// [GCC-WRAPPER-TEXT] — diagnostic comes from GCC's strong-enum
// type-mismatch rejection.

#include <crucible/algebra/lattices/DetSafeLattice.h>
#include <crucible/algebra/lattices/HotPathLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    HotPathTier hot_path_val = HotPathTier::Hot;
    DetSafeTier det_safe_val = DetSafeTier::Pure;

    // Should FAIL: DetSafeLattice::leq's signature requires two
    // DetSafeTier values; passing a HotPathTier as the second
    // argument is a type-mismatch (HotPathTier is NOT convertible
    // to DetSafeTier — both are `enum class : uint8_t` so no
    // implicit narrowing applies).
    return static_cast<int>(
        DetSafeLattice::leq(det_safe_val, hot_path_val));
}
