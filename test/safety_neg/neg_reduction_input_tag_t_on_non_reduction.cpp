// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D14-AUDIT — sibling-pattern parity with
// neg_unary_transform_input_tag_t_on_non_unary.cpp and
// neg_binary_transform_lhs_tag_t_on_non_binary.cpp.
//
// Violation: instantiating reduction_input_tag_t<FnPtr> on a function
// pointer whose signature is not the canonical Reduction shape.  The
// alias is constrained on `requires Reduction<FnPtr>`; non-matching
// signatures must be rejected at the requires clause, not by chaining
// through param_type_t / owned_region_tag_t (which would surface a
// confusing two-level diagnostic naming the harvested region-tag
// mismatch instead of the parameter-shape mismatch).
//
// Concrete bug-class this catches: a refactor that drops the
// `requires Reduction<FnPtr>` constraint on reduction_input_tag_t
// would let `reduction_input_tag_t<&fn>` for a non-matching `fn`
// resolve via param_type_t<FnPtr, 0> + owned_region_tag_t — both
// constrained, but the failure cascade traverses two requires clauses
// and surfaces a "constraints not satisfied for owned_region_tag_t"
// diagnostic far from the real bug (the function isn't a Reduction).
// With the Reduction constraint, the failure is a single clean
// rejection naming Reduction itself.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/Reduction.h>

inline void neg_witness_int(int) noexcept {}

int main() {
    // arity 1, parameter is `int` — not a Reduction.
    using Tag = crucible::safety::extract::reduction_input_tag_t<
        &::neg_witness_int>;
    Tag const t{};
    (void)t;
    return 0;
}
