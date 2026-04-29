// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D12 fixture — third member of the constrained-extractor
// sister set.  unary_transform_output_tag_t differs from input_tag_t
// / input_value_t in that it routes through a detail-namespace
// `unary_transform_output_tag_select` template specialized on
// `IsInPlace = std::is_void_v<return_type_t<FnPtr>>`.  Without the
// `requires UnaryTransform<FnPtr>` constraint on the alias, a
// refactor that drops the gate would let the alias instantiate the
// detail dispatcher with a non-UnaryTransform FnPtr — producing an
// opaque "no type named type in unary_transform_output_tag_select"
// substitution failure deep inside the dispatcher rather than a
// clean requires-clause rejection.
//
// This fixture probes a non-rvalue-reference parameter shape (lvalue
// reference to OwnedRegion — the borrow-not-consume case from the
// header doc), which produces a UnaryTransform-false on the
// rvalue-ref clause specifically.  Distinct from the other two
// fixtures' arity / non-region cases.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/UnaryTransform.h>
#include <crucible/safety/OwnedRegion.h>

namespace { struct out_tag_neg_test {}; }

inline void neg_witness_lvalue_ref(
    crucible::safety::OwnedRegion<int, ::out_tag_neg_test>&) noexcept {}

int main() {
    // Lvalue reference to OwnedRegion — fails UnaryTransform's
    // is_rvalue_reference_v clause.
    using Tag = crucible::safety::extract::unary_transform_output_tag_t<
        &::neg_witness_lvalue_ref>;
    Tag const t{};
    (void)t;
    return 0;
}
