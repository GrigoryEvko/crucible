// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: claiming HappensBeforeLattice's element_type satisfies
// the C++20 `std::three_way_comparable` concept against `std::
// strong_ordering`.
//
// Vector clocks form a PARTIAL order (concurrent events satisfy
// neither <, ==, nor >); element_type's `operator<=>` returns
// `std::partial_ordering`.  std::partial_ordering is NOT convertible
// to std::strong_ordering — strong_ordering carries the additional
// guarantee that no two distinct elements are unordered.  The concept
// `three_way_comparable<T, R>` requires the result of `<=>` to be
// implicitly convertible to R; since partial → strong fails that,
// the concept fails for (element_type, strong_ordering).
//
// This test pins the partial-order semantic at the type level.  A
// future refactor that defaulted operator<=> on element_type (which
// would yield strong_ordering via the std::array carrier's
// lexicographic order) would silently change the algebraic shape and
// break the Cipher::ReplayLog use case where concurrent events MUST
// be observable as `unordered`.  This test would START passing after
// such a regression — and would FAIL the neg-compile harness — making
// the regression visible.
//
// [FRAMEWORK-CONTROLLED] — diagnostic regex matches the exact text of
// the static_assert message below.

#include <crucible/algebra/lattices/HappensBefore.h>

#include <compare>

using namespace crucible::algebra::lattices;

int main() {
    using HB4 = HappensBeforeLattice<4>;

    // Should FAIL: element_type's operator<=> returns
    // std::partial_ordering (concurrent events are unordered), not
    // std::strong_ordering (which forbids unordered).  The concept
    // gate rejects the (T, strong_ordering) pair.
    static_assert(std::three_way_comparable<HB4::element_type,
                                            std::strong_ordering>,
        "[VectorClockMustBePartialOrder] HappensBeforeLattice's "
        "element_type<=> returns partial_ordering and MUST NOT be "
        "convertible to strong_ordering — concurrent events are "
        "unordered, which strong_ordering forbids by definition.  "
        "If this static_assert fires GREEN (as a positive-compile), "
        "the lattice's <=> has been silently widened to "
        "strong_ordering and Cipher::ReplayLog's concurrency "
        "semantics are broken.");

    return 0;
}
