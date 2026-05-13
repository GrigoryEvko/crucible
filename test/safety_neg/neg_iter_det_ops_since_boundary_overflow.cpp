// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling .bump() on IterationDetector::OpsSinceBoundary
// when current value is at numeric_limits<uint32_t>::max() — fires
// Monotonic's overflow contract.
//
// Per WRAP-IterDet-3 (#929), IterationDetector::OpsSinceBoundary is
// safety::Monotonic<uint32_t>.  Monotonic::bump() carries
// pre(impl_.peek() != std::numeric_limits<T>::max()) so the increment
// cannot wrap.  In constexpr context (constant evaluation), a
// contract violation makes the expression non-constant per P1494R5
// — using it where a constant is required is ill-formed.
//
// Companion fixture to neg_iter_det_ops_since_boundary_regression.cpp:
//   - That one tests monotonicity (advance to smaller value).
//   - This one tests overflow (bump at UINT32_MAX = wraparound edge).
//     Catches a future regression that drops the overflow guard
//     (e.g. switches to plain `value_++` without the != max() check),
//     leaving the counter to silently wrap from UINT32_MAX → 0 —
//     which would also be a monotonicity violation, but the bump()
//     contract is the only line of defense at the type level.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.  Different mutation paths
// (advance vs bump) and different invariants (monotonicity vs
// overflow) cover different drift modes; both fixtures together pin
// the wrapper's contracts.

#include <crucible/ir001/IterationDetector.h>

#include <climits>
#include <cstdint>

// Constexpr function that triggers the overflow contract.  Calling it
// in a constant-evaluated context (the constexpr local below) makes
// the result non-constant, hence ill-formed.
constexpr crucible::IterationDetector::OpsSinceBoundary make_bad() {
    crucible::IterationDetector::OpsSinceBoundary m{uint32_t{UINT32_MAX}};
    m.bump();  // pre: peek() != UINT32_MAX → false → contract failure
    return m;
}

int main() {
    constexpr auto bad = make_bad();
    (void)bad;
    return 0;
}
