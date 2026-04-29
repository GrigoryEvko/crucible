// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D12 fixture — sister to neg_unary_transform_input_tag_t.
// Each of the three constrained extractors on UnaryTransform.h
// (input_tag_t / input_value_t / output_tag_t) carries its
// `requires UnaryTransform<FnPtr>` clause INDEPENDENTLY.  A refactor
// that drops the constraint on input_value_t while leaving the other
// two correctly constrained would slip through the input_tag_t
// neg-compile but is caught here.
//
// This fixture probes a function with arity-MISMATCH (binary instead
// of unary) — distinct from the input_tag_t fixture which probes
// non-region-parameter.  The two fixtures together cover both shape-
// recognition failure modes (arity vs parameter type) for the
// extractor's constraint.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/UnaryTransform.h>

inline void neg_witness_binary(int, int) noexcept {}

int main() {
    // arity 2 → fails UnaryTransform's `arity_v == 1` clause.
    using V = crucible::safety::extract::unary_transform_input_value_t<
        &::neg_witness_binary>;
    V const v{};
    (void)v;
    return 0;
}
