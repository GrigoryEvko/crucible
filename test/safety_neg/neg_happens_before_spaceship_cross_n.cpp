// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: applying operator<=> across HappensBeforeLattice<N>
// instantiations with different N.
//
// Symmetric to neg_happens_before_cross_n_mixing.cpp but for the
// CANONICAL C++20 client-syntax surface (the spaceship), not just
// the static `leq` member.  Pins that the partial-order spaceship
// also respects per-N type distinction — a future refactor that
// added a converting constructor on element_type or made operator<=>
// itself templated over (N1, N2) would silently bridge across
// participant counts at the most idiomatic call site users reach
// for (`if (a < b)`).
//
// This test catches ONE specific class of bug the cross-N `leq` test
// would NOT catch: a refactor that left `leq` strict but made the
// spaceship lenient.  Both surfaces must remain N-strict.
//
// [GCC-WRAPPER-TEXT] — operator-resolution rejection on cross-type
// spaceship.

#include <crucible/algebra/lattices/HappensBefore.h>

#include <compare>

using namespace crucible::algebra::lattices;

int main() {
    HappensBeforeLattice<3>::element_type clock_3p{{1, 0, 0}};
    HappensBeforeLattice<4>::element_type clock_4p{{1, 0, 0, 0}};

    // Should FAIL: operator<=> is defined as a member of element_type
    // taking `element_type const&` — the parameter type is bound to
    // THIS specific instantiation's element_type.  Cross-N invocation
    // is rejected at overload resolution.
    [[maybe_unused]] auto ord = clock_3p <=> clock_4p;
    return 0;
}
