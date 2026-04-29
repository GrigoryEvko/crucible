// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: indexing param_type_t<FnPtr, 0> on a nullary function
// (arity == 0).  Distinct from neg_signature_traits_param_index_
// overflow.cpp because index 0 is the boundary case — a refactor that
// changed `requires (I < arity)` to `requires (I <= arity)` (off by
// one) would let this slip through while the explicit-overflow case
// still rejects.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/SignatureTraits.h>

inline void neg_witness_nullary() noexcept {}

int main() {
    // arity_v<&neg_witness_nullary> == 0, so index 0 is out of range
    // (any index is, since the parameter list is empty).
    using OutOfRange =
        crucible::safety::extract::param_type_t<&::neg_witness_nullary, 0>;

    OutOfRange const value{};
    (void)value;
    return 0;
}
