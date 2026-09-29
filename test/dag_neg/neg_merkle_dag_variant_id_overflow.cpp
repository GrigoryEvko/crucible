// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling .bump() on RegionNode::VariantCounter when the value
// is numeric_limits<uint32_t>::max(), which fires Monotonic's overflow
// precondition.
//
// RegionNode::VariantCounter is ::fixy::Monotonic<uint32_t>.  bump()
// carries CRUCIBLE_PRE(current != numeric_limits<T>::max()), so the
// increment cannot wrap.  In a constant evaluation the failed precondition
// reaches a non-constant trap, and the evaluation fails.
//
// Companion fixture to neg_merkle_dag_variant_id_regression.cpp:
//   - That one tests monotonicity (advance to a smaller id).
//   - This one tests overflow (bump at UINT32_MAX).  A counter that wraps
//     to 0 would claim that no variant is selected.

#include <crucible/MerkleDag.h>
#include <fixy/Mutation.h>

#include <cstdint>

constexpr crucible::RegionNode::VariantCounter make_bad() {
    auto counter = ::fixy::mint_monotonic<uint32_t>(uint32_t{UINT32_MAX});
    counter.bump();
    return counter;
}

int main() {
    constexpr auto bad = make_bad();
    (void)bad;
    return 0;
}
