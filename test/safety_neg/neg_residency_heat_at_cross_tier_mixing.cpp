// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a ResidencyHeatLattice::At<TIER_B>::element_type
// to a function expecting ResidencyHeatLattice::At<TIER_A>::element_type.
//
// Per-At<T> nested-struct-identity at the LATTICE substrate.
// Mirrors the per-At<T> mixing fixtures shipped for the seven
// sister chain lattices.  A future refactor extracting a shared
// singleton_carrier<ResidencyHeatTag> alias above At<T> would
// silently allow Hot-tier and Cold-tier values to interconvert at
// the lattice level, bypassing the working-set-residency discipline
// via Graded's compose / weaken paths.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection on the nested-
// struct template identity at the LATTICE surface.

#include <crucible/algebra/lattices/ResidencyHeatLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    ResidencyHeatLattice::At<ResidencyHeatTag::Hot>::element_type  hot_elt{};
    ResidencyHeatLattice::At<ResidencyHeatTag::Cold>::element_type cold_elt{};

    // Should FAIL: At<Hot>::leq expects two At<Hot>::element_type
    // arguments; cold_elt is At<Cold>::element_type.
    return static_cast<int>(
        ResidencyHeatLattice::At<ResidencyHeatTag::Hot>::leq(hot_elt, cold_elt));
}
