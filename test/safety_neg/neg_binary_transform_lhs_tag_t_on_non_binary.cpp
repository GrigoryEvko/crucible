// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D13 fixture — first audit of the constrained extractors on
// BinaryTransform.h.  binary_transform_lhs_tag_t is constrained on
// `requires BinaryTransform<FnPtr>`; instantiating it on a
// non-BinaryTransform-shape function pointer must fail at the
// requires clause.
//
// Concrete bug-class this catches: a refactor that drops the
// `requires BinaryTransform<FnPtr>` constraint on
// binary_transform_lhs_tag_t (or any of its FIVE siblings —
// rhs_tag_t / lhs_value_t / rhs_value_t / output_tag_t /
// has_same_tag_v) would let the extractors silently chain through
// `param_type_t<FnPtr, 0>` followed by `owned_region_tag_t`,
// producing a constraint cascade that lands deep inside
// owned_region_tag_t's requires clause rather than at the binary-
// transform-shape-recognition boundary.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/BinaryTransform.h>

inline void neg_witness_two_ints(int, int) noexcept {}

int main() {
    // arity 2 BUT parameters are int (not OwnedRegion).
    // BinaryTransform<&neg_witness_two_ints> is false.
    using Tag = crucible::safety::extract::binary_transform_lhs_tag_t<
        &::neg_witness_two_ints>;
    Tag const t{};
    (void)t;
    return 0;
}
