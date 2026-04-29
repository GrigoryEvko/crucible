// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D13 fixture — third member of the constrained-extractor
// audit set.  binary_transform_output_tag_t routes through a
// detail-namespace `binary_transform_output_tag_select` template,
// mirroring the unary version's IsInPlace dispatcher.  Without the
// `requires BinaryTransform<FnPtr>` constraint, a refactor that
// drops the gate would let the alias instantiate the detail
// dispatcher with a non-BinaryTransform FnPtr — producing an opaque
// "no type named type in binary_transform_output_tag_select"
// substitution failure deep inside the dispatcher rather than a
// clean requires-clause rejection.
//
// This fixture probes a binary function whose return type is `int`
// (non-region non-void) — fails the return-clause specifically.
// Distinct from the lhs/rhs fixtures' arity / non-region-parameter
// cases; together they cover all three orthogonal failure modes
// (arity / parameter shape / return shape).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/OwnedRegion.h>

namespace {
struct lhs_neg_tag {};
struct rhs_neg_tag {};
}  // namespace

inline int neg_witness_int_return(
    crucible::safety::OwnedRegion<int, ::lhs_neg_tag>&&,
    crucible::safety::OwnedRegion<int, ::rhs_neg_tag>&&) noexcept
{
    return 0;
}

int main() {
    // Both inputs are valid OwnedRegion&&, but return type is `int`
    // → fails BinaryTransform's `return_type is void or OwnedRegion`
    // clause.  The output_tag_t extractor must reject this at its
    // requires clause.
    using Tag = crucible::safety::extract::binary_transform_output_tag_t<
        &::neg_witness_int_return>;
    Tag const t{};
    (void)t;
    return 0;
}
