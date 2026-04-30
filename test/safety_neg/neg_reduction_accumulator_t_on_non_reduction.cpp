// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D14-AUDIT — locks in the requires-clause on
// reduction_accumulator_t.  Critical because the FOUND-F04
// parallel_reduce_views<N, R> lowering uses this alias to type its
// per-worker partial accumulator vector — a refactor that drops the
// constraint would let the dispatcher silently allocate a
// std::vector<void> for non-Reduction functions, then fail much
// later with an opaque "vector<void> ill-formed" diagnostic.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/Reduction.h>

inline void neg_witness_int(int) noexcept {}

int main() {
    // arity 1, not a Reduction → reduction_accumulator_t<&fn>
    // ill-formed at the alias declaration.
    using R = crucible::safety::extract::reduction_accumulator_t<
        &::neg_witness_int>;
    R const r{};
    (void)r;
    return 0;
}
