// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a Tolerance value to LifetimeLattice::leq
// alongside a Lifetime value (or vice versa).
//
// Closes the THIRD edge of the chain-lattice cross-mixing triangle:
//
//   neg_consistency_cross_lattice_mixing → Consistency × Lifetime
//   neg_tolerance_cross_lattice_mixing   → Consistency × Tolerance
//   neg_lifetime_tolerance_cross_lattice (THIS) → Lifetime × Tolerance
//
// Together the three neg-compiles cover ALL three pairs in the
// {Lifetime, Consistency, Tolerance} cross-mixing triangle.  A
// future enum-collapse refactor that quietly merged any pair into
// a shared `Tier` would now be caught by at least two of the three
// neg-compiles (the merged pair plus any pair involving one of the
// merged enums).
//
// [GCC-WRAPPER-TEXT] — strong-enum type-mismatch rejection.  Same
// rationale as the other two cross-mixing tests.

#include <crucible/algebra/lattices/LifetimeLattice.h>
#include <crucible/algebra/lattices/ToleranceLattice.h>

using namespace crucible::algebra::lattices;

int main() {
    Lifetime  lifetime_val  = Lifetime::PER_FLEET;
    Tolerance tolerance_val = Tolerance::BITEXACT;

    // Should FAIL: LifetimeLattice::leq's signature requires two
    // Lifetime values; Tolerance is a structurally different
    // `enum class : uint8_t` and not implicitly convertible.
    return static_cast<int>(
        LifetimeLattice::leq(lifetime_val, tolerance_val));
}
