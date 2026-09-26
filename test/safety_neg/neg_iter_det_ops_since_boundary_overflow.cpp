// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling .bump() on IterationDetector::OpsSinceBoundary when
// the value is numeric_limits<uint32_t>::max(), which fires Monotonic's
// overflow precondition.
//
// IterationDetector::OpsSinceBoundary is ::fixy::Monotonic<uint32_t>.
// Monotonic::bump() carries CRUCIBLE_PRE(current != numeric_limits<T>::max()),
// so the increment cannot wrap.  In a constant evaluation the failed
// precondition reaches a non-constant trap, and the evaluation fails.
//
// Companion fixture to neg_iter_det_ops_since_boundary_regression.cpp:
//   - That one tests monotonicity (advance to a smaller value).
//   - This one tests overflow (bump at UINT32_MAX).  A regression that
//     drops the overflow guard lets the counter wrap to 0, which is also
//     a monotonicity violation, but bump() is the only gate that sees it.

#include <crucible/IterationDetector.h>
#include <fixy/Mutation.h>

#include <cstdint>

constexpr crucible::IterationDetector::OpsSinceBoundary make_bad() {
    auto counter = ::fixy::mint_monotonic<uint32_t>(uint32_t{UINT32_MAX});
    counter.bump();
    return counter;
}

int main() {
    constexpr auto bad = make_bad();
    (void)bad;
    return 0;
}
