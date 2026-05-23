// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-184 ClockSourceLattice, mismatch class #2 of 2:
// PER-SLOT AXIS DISJOINTNESS.
//
// The product element's slot 0 STRICTLY holds a `DetSafeTier` (the
// replay-safety axis); slot 1 a `SuspendBehavior`; slot 2 a
// `PinningRequirement`.  `get<0>(point)` returns a `DetSafeTier&`.
// Assigning a foreign-axis enum (`SuspendBehavior`) into the DetSafe
// slot is a strong-enum type mismatch — `enum class` admits NO implicit
// cross-enum conversion.  This pins the TypeSafe disjointness the
// composite depends on: the three clock-quality axes never bleed into
// each other's slot, so a cross-axis grade swap that would corrupt the
// V-185 federation-cache key cannot happen silently.
//
// Distinct from neg_clock_source_cross_vocabulary_mixing.cpp, which
// fails at a lattice OP argument; here the failure is a direct per-slot
// grade element_type assignment between two sibling axes.
//
// Expected diagnostic: no match for / cannot convert / conversion from /
// no viable / invalid conversion.

#include <crucible/algebra/lattices/ClockSourceLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    ClockSourceLattice::element_type point = ClockSourceLattice::bottom();

    // Should FAIL: slot 0 is a DetSafeTier; a SuspendBehavior (the
    // sibling suspend axis) cannot be assigned into it — distinct strong
    // enums with no implicit conversion.
    ClockSourceLattice::get<0>(point) = SuspendBehavior::KeepsTicking;

    return static_cast<int>(
        ClockSourceLattice::get<0>(point) == DetSafeTier::Pure);
}
