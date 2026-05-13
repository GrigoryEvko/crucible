// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling .advance(smaller) on IterationDetector::
// OpsSinceBoundary in a constexpr context — fires Monotonic's
// monotonicity contract.
//
// Per WRAP-IterDet-3 (#929), IterationDetector::OpsSinceBoundary is
// safety::Monotonic<uint32_t>.  Monotonic::advance(new_value) carries
// pre(lattice_type::leq(peek(), new_value)) — i.e. new_value must be
// >= current.  In constexpr context (constant evaluation), a contract
// violation makes the expression non-constant per P1494R5 — using it
// where a constant is required is ill-formed.
//
// Companion fixture to neg_iter_det_ops_since_boundary_overflow.cpp:
//   - This one is the boundary edge (current=10, advance(5) → 5 < 10).
//     Catches off-by-one drift in the monotonicity predicate (e.g.
//     a future regression that uses `>` instead of `>=`).
//   - That one is the wide miss (bump() at UINT32_MAX → overflow).
//     Catches "drop the overflow contract" regression.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.  Together they pin the
// monotonicity AND overflow guards on this counter at the type-system
// level.

#include <crucible/ir001/IterationDetector.h>

#include <cstdint>

// Constexpr function that triggers the monotonicity contract.  Calling
// it in a constant-evaluated context (the constexpr local below) makes
// the result non-constant, hence ill-formed.
constexpr crucible::IterationDetector::OpsSinceBoundary make_bad() {
    crucible::IterationDetector::OpsSinceBoundary m{uint32_t{10}};
    m.advance(uint32_t{5});  // pre: 5 >= 10 → false → contract failure
    return m;
}

int main() {
    constexpr auto bad = make_bad();
    (void)bad;
    return 0;
}
