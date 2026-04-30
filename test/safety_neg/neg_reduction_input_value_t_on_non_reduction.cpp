// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D14-AUDIT — sibling-pattern parity with
// neg_unary_transform_input_value_t_on_non_unary.cpp.
//
// Violation: instantiating reduction_input_value_t<FnPtr> on a
// function whose signature is not the canonical Reduction shape.
// The alias is constrained on `requires Reduction<FnPtr>` — without
// the constraint, the alias would chain through
// owned_region_value_t and surface a confusing inner diagnostic
// rather than naming the parameter-shape mismatch.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/Reduction.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    // arity 2 but neither parameter is OwnedRegion or reduce_into —
    // not a Reduction.
    using V = crucible::safety::extract::reduction_input_value_t<
        &::neg_witness_two_ints>;
    V const v{};
    (void)v;
    return 0;
}
