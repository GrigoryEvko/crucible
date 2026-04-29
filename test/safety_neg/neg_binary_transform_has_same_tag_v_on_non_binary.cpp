// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D13 fixture — pins the `requires BinaryTransform<FnPtr>`
// constraint on `binary_transform_has_same_tag_v` specifically.
// Distinct from the four type-alias siblings (lhs_tag_t / rhs_tag_t /
// lhs_value_t / rhs_value_t / output_tag_t) because has_same_tag_v
// is a VARIABLE TEMPLATE projecting a bool, NOT a type alias.  The
// constraint propagation surface for variable templates is
// structurally distinct from type aliases (the constraint guards
// initialization rather than alias resolution), so an independent
// neg-fixture is required to lock it in.
//
// A refactor that drops the `requires BinaryTransform<FnPtr>` clause
// from the variable template would let
// `binary_transform_has_same_tag_v<&fn>` for non-binary `fn`
// chain through `binary_transform_lhs_tag_t<&fn>` /
// binary_transform_rhs_tag_t<&fn>` (each ALSO constrained, but the
// failure message points at owned_region_tag_t not at the binary-
// transform-shape mismatch).  With the constraint, the failure
// names BinaryTransform directly.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/BinaryTransform.h>

inline void neg_witness_nullary() noexcept {}

int main() {
    // arity 0 → fails BinaryTransform's `arity_v == 2` clause.
    constexpr bool same =
        crucible::safety::extract::binary_transform_has_same_tag_v<
            &::neg_witness_nullary>;
    (void)same;
    return 0;
}
