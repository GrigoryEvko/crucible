// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling .advance(smaller) on RegionNode::VariantCounter
// in a constexpr context — fires Monotonic's monotonicity contract.
//
// Per WRAP-MerkleDag-6 (#942), RegionNode::variant_id is
// safety::Monotonic<uint32_t> (alias `RegionNode::VariantCounter`).
// Monotonic::advance(new_value) carries
// pre(lattice_type::leq(peek(), new_value)) — i.e. new_value must be
// >= current.  In constexpr context (constant evaluation), a contract
// violation makes the expression non-constant per P1494R5 — using it
// where a constant is required is ill-formed.
//
// Companion fixture to neg_merkle_dag_variant_id_overflow.cpp:
//   - This one is the boundary edge (current=10, advance(5) → 5 < 10).
//     Catches off-by-one drift in the monotonicity predicate (e.g.
//     a future regression that uses `>` instead of `>=`).
//   - That one is the wide miss (bump() at UINT32_MAX → overflow).
//     Catches "drop the overflow contract" regression.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.  Together they pin the
// monotonicity AND overflow guards on RegionNode::variant_id at the
// type-system level — enforcing the "variant_id never goes backward"
// invariant the original set_variant comment intended but had no
// type-level witness for.

#include <crucible/MerkleDag.h>

#include <cstdint>

// Constexpr function that triggers the monotonicity contract.  Calling
// it in a constant-evaluated context (the constexpr local below) makes
// the result non-constant, hence ill-formed.
constexpr crucible::RegionNode::VariantCounter make_bad() {
    crucible::RegionNode::VariantCounter m{uint32_t{10}};
    m.advance(uint32_t{5});  // pre: 5 >= 10 → false → contract failure
    return m;
}

int main() {
    constexpr auto bad = make_bad();
    (void)bad;
    return 0;
}
