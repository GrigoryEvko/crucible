// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a HappensBeforeLattice<N1>::element_type to a
// function expecting HappensBeforeLattice<N2>::element_type for
// N1 ≠ N2.
//
// HappensBeforeLattice<N>::element_type wraps a std::array<uint64_t,
// N>, so the carrier's identity depends on N at the type level.  A
// vector clock for 3 participants has structurally different storage
// from a vector clock for 4 participants — they MUST not be
// interchangeable, even if the values "happen" to be padded.
//
// Pins the cross-N type-safety contract: a future refactor that
// (e.g.) added a templated converting constructor from
// element_type<N1> to element_type<N2> would silently allow a
// 3-participant clock to flow into a 4-participant lattice op,
// breaking the per-clock causal-precedence claim that depends on
// every slot being addressed correctly.
//
// [GCC-WRAPPER-TEXT] — overload-resolution rejection.  Acceptable
// because element_type<N> is a structurally-different nested struct
// per N; no framework-owned static_assert is needed to enforce the
// distinction.

#include <crucible/algebra/lattices/HappensBefore.h>

using namespace crucible::algebra::lattices;

int main() {
    HappensBeforeLattice<3>::element_type clock_3p{{1, 0, 0}};
    HappensBeforeLattice<4>::element_type clock_4p{{1, 0, 0, 0}};

    // Should FAIL: HappensBeforeLattice<4>::leq expects two
    // HappensBeforeLattice<4>::element_type arguments; clock_3p is
    // a different type (HappensBeforeLattice<3>::element_type).
    return static_cast<int>(
        HappensBeforeLattice<4>::leq(clock_4p, clock_3p));
}
