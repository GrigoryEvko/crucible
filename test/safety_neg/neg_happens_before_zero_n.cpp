// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating HappensBeforeLattice<N=0>.
//
// Vector clocks with zero participants have no algebraic content —
// every two "vectors" would be trivially equal and the bottom/top
// distinction collapses.  The lattice's primary template carries
// `static_assert(N > 0, "HappensBeforeLattice<0> is forbidden — ...")`
// to reject this at the type level.
//
// This test pins the contract: future refactors of HappensBeforeLattice
// must NOT relax the N > 0 check (which would let downstream callers
// instantiate degenerate clocks, breaking causal_merge / successor_at
// invariants that depend on at least one slot existing).
//
// [FRAMEWORK-CONTROLLED] — diagnostic regex matches the exact
// static_assert message in HappensBefore.h's primary template.

#include <crucible/algebra/lattices/HappensBefore.h>

using namespace crucible::algebra::lattices;

int main() {
    // Should FAIL: N=0 trips the static_assert in the primary template.
    using HB0 = HappensBeforeLattice<0>;
    HB0::element_type e{};
    return static_cast<int>(HB0::leq(e, e));
}
