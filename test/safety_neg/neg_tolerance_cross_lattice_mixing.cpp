// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a Tolerance value to ConsistencyLattice::leq
// alongside a Consistency value (or vice versa).
//
// Symmetric to neg_consistency_cross_lattice_mixing.cpp but pinned
// across the third pairing (Tolerance × Consistency) — confirms the
// strong-enum discipline holds for ALL pairs of the three new chain
// lattices, not just one.  Without this test a future enum-collapse
// refactor that quietly merged Tolerance and Consistency into a
// shared `Tier` could pass the consistency-vs-lifetime neg-compile
// while still letting Tolerance values flow into Consistency
// positions — defeating the §10 precision-budget calibrator's
// per-op classification that BatchPolicy<Axis, Level> consults.
//
// [GCC-WRAPPER-TEXT] — same rationale as the consistency-vs-lifetime
// test.

#include <crucible/algebra/lattices/ConsistencyLattice.h>
#include <crucible/algebra/lattices/ToleranceLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    Consistency consistency_val = Consistency::STRONG;
    Tolerance   tolerance_val   = Tolerance::BITEXACT;

    // Should FAIL: ConsistencyLattice::leq expects two Consistency
    // values; Tolerance is a structurally different `enum class :
    // uint8_t` and not implicitly convertible.
    return static_cast<int>(
        ConsistencyLattice::leq(consistency_val, tolerance_val));
}
