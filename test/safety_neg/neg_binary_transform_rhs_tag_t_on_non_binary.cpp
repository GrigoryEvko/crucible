// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D13 fixture — sister to neg_binary_transform_lhs_tag_t.
// Each of the SIX constrained extractors on BinaryTransform.h
// (lhs_tag_t / rhs_tag_t / lhs_value_t / rhs_value_t / output_tag_t /
// has_same_tag_v) carries its `requires BinaryTransform<FnPtr>`
// clause INDEPENDENTLY.  This fixture pins the constraint on
// rhs_tag_t specifically, probing arity-MISMATCH (unary instead of
// binary) — distinct from the lhs_tag_t fixture which probes non-
// region parameters.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/BinaryTransform.h>

inline void neg_witness_unary_int(int) noexcept {}

int main() {
    // arity 1 → fails BinaryTransform's `arity_v == 2` clause.
    using Tag = crucible::safety::extract::binary_transform_rhs_tag_t<
        &::neg_witness_unary_int>;
    Tag const t{};
    (void)t;
    return 0;
}
