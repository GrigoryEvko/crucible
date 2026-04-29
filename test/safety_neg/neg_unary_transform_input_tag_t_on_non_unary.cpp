// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D12 fixture — first audit of the constrained extractors on
// UnaryTransform.h.  unary_transform_input_tag_t is constrained on
// `requires UnaryTransform<FnPtr>`; instantiating it on a
// non-UnaryTransform-shape function pointer must fail at the
// requires clause, not at the alias body.
//
// Concrete bug-class this catches: a refactor that drops the
// `requires UnaryTransform<FnPtr>` constraint on
// unary_transform_input_tag_t (or any of its three siblings — input
// _value_t / output_tag_t) would let
// `unary_transform_input_tag_t<&fn>` for a non-matching `fn` resolve
// by chaining through `param_type_t<FnPtr, 0>` followed by
// `owned_region_tag_t` — both of which are constrained, but the
// failure cascade traverses two requires clauses and surfaces a
// "constraints not satisfied for owned_region_tag_t" diagnostic.
// With the UnaryTransform constraint, the failure is a single clean
// rejection naming UnaryTransform itself — pointing the developer
// directly at the parameter-shape mismatch rather than the harvested
// region-tag mismatch.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/UnaryTransform.h>

inline void neg_witness_int(int) noexcept {}

int main() {
    // arity 1, but parameter is `int` (not OwnedRegion).
    // UnaryTransform<&neg_witness_int> is false.
    using Tag = crucible::safety::extract::unary_transform_input_tag_t<
        &::neg_witness_int>;
    Tag const t{};
    (void)t;
    return 0;
}
