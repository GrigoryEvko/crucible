// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: indexing param_type_t<FnPtr, I> with I >= arity_v<FnPtr>.
//
// Tests the `requires (I < arity)` constraint clause on
// signature_traits<FnPtr>::param_type_t<I>.  An out-of-range
// parameter index must produce a constraint-failure diagnostic, not
// a deep template-substitution error inside std::define_static_array
// or std::span::operator[].
//
// Concrete bug-class this catches: a refactor that drops the
// `requires (I < arity)` clause from the alias would let
// param_type_t<&fn, 999> trigger a runtime out-of-bounds in the
// span access at consteval, producing an opaque "reflection does not
// have a type" or "index out of range" error chain.  With the
// constraint, the diagnostic is clean: "constraints not satisfied"
// pointing at the alias declaration.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/SignatureTraits.h>

inline void neg_witness_unary(int) noexcept {}

int main() {
    // arity_v<&neg_witness_unary> == 1, so index 0 is valid; index 1
    // is out of range and must be rejected by the requires clause.
    using OutOfRange =
        crucible::safety::extract::param_type_t<&::neg_witness_unary, 1>;

    OutOfRange const value{};
    (void)value;
    return 0;
}
