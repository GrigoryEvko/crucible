// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling .advance(smaller) on IterationDetector::
// OpsSinceBoundary in a constexpr context, which fires Monotonic's
// monotonicity precondition.
//
// IterationDetector::OpsSinceBoundary is ::fixy::Monotonic<uint32_t>.
// Monotonic::advance(new_value) carries
// CRUCIBLE_PRE(lattice_type::leq(current, new_value)), so new_value must
// not be less than the current value.  In a constant evaluation the
// failed precondition reaches a non-constant trap, and the evaluation
// fails.
//
// Companion fixture to neg_iter_det_ops_since_boundary_overflow.cpp:
//   - This one is the boundary edge (current = 10, advance(5)).  It
//     catches drift in the monotonicity predicate.
//   - That one is the wide miss (bump() at UINT32_MAX), the overflow
//     guard.

#include <crucible/IterationDetector.h>
#include <fixy/Mutation.h>

#include <cstdint>

constexpr crucible::IterationDetector::OpsSinceBoundary make_bad() {
    auto counter = ::fixy::mint_monotonic<uint32_t>(uint32_t{10});
    counter.advance(uint32_t{5});
    return counter;
}

int main() {
    constexpr auto bad = make_bad();
    (void)bad;
    return 0;
}
