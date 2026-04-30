// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F06 fixture — pins IsFusable concept rejection on arity
// mismatch.  Producer is unary int→int; consumer is binary
// (int, int)→int.  V1 can_fuse_v requires arity_v<Fn2> == 1.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/Fusion.h>

inline int producer(int x) noexcept { return x; }
inline int consumer_binary(int a, int b) noexcept { return a + b; }

template <auto F1, auto F2>
    requires crucible::safety::IsFusable<F1, F2>
inline void fuse_only_if_fusable() noexcept {}

int main() {
    fuse_only_if_fusable<&producer, &consumer_binary>();
    return 0;
}
