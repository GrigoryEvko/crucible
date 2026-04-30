// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D14-AUDIT — locks in the requires-clause on
// reduction_reducer_t.  The dispatcher's parallel_reduce_views<N, R>
// lowering uses this alias to type the per-worker reducer.  Without
// the Reduction constraint, the alias would chain through
// reduce_into_reducer_t and surface a "constraints not satisfied for
// reduce_into_reducer_t" diagnostic — accurate but pointing at the
// wrong layer.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/Reduction.h>

inline int neg_witness_returns_int(int) noexcept { return 0; }

int main() {
    // arity 1, non-void return → not a Reduction.
    using Op = crucible::safety::extract::reduction_reducer_t<
        &::neg_witness_returns_int>;
    Op const op{};
    (void)op;
    return 0;
}
