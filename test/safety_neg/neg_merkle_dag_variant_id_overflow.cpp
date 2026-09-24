// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling .bump() on RegionNode::VariantCounter when
// current value is at numeric_limits<uint32_t>::max() — fires
// Monotonic's overflow contract.
//
// Per WRAP-MerkleDag-6 (#942), RegionNode::variant_id is
// safety::Monotonic<uint32_t> (alias `RegionNode::VariantCounter`).
// Monotonic::bump() carries
// pre(impl_.peek() != std::numeric_limits<T>::max()) so the
// increment cannot wrap.  In constexpr context (constant evaluation),
// a contract violation makes the expression non-constant per
// P1494R5 — using it where a constant is required is ill-formed.
//
// Companion fixture to neg_merkle_dag_variant_id_regression.cpp:
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
// the wrapper's contracts on RegionNode::variant_id.

#include <crucible/MerkleDag.h>

#include <climits>
#include <cstdint>

// Constexpr function that triggers the overflow contract.  Calling it
// in a constant-evaluated context (the constexpr local below) makes
// the result non-constant, hence ill-formed.
constexpr crucible::RegionNode::VariantCounter make_bad() {
    crucible::RegionNode::VariantCounter m{uint32_t{UINT32_MAX}};
    m.bump();  // pre: peek() != UINT32_MAX → false → contract failure
    return m;
}

int main() {
    constexpr auto bad = make_bad();
    (void)bad;
    return 0;
}
