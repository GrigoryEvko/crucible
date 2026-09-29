// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling .advance(smaller) on RegionNode::VariantCounter,
// which fires Monotonic's monotonicity precondition.
//
// RegionNode::VariantCounter is ::fixy::Monotonic<uint32_t>.  Variants are
// registered in increasing order, so the active id only moves forward.
// advance(new_value) carries CRUCIBLE_PRE(lattice_type::leq(current,
// new_value)).  In a constant evaluation the failed precondition reaches a
// non-constant trap, and the evaluation fails.
//
// Companion fixture to neg_merkle_dag_variant_id_overflow.cpp:
//   - This one is the boundary edge (current = 10, advance(5)).
//   - That one is the wide miss (bump() at UINT32_MAX).

#include <crucible/MerkleDag.h>
#include <fixy/Mutation.h>

#include <cstdint>

constexpr crucible::RegionNode::VariantCounter make_bad() {
    auto counter = ::fixy::mint_monotonic<uint32_t>(uint32_t{10});
    counter.advance(uint32_t{5});
    return counter;
}

int main() {
    constexpr auto bad = make_bad();
    (void)bad;
    return 0;
}
