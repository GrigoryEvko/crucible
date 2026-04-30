// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F07 fixture — pins the user-visible substitution-failure
// surface of `safety::fuse<>()`.  The existing F06 fixture exercises
// IsFusable through a custom local template; this one calls fuse<>()
// directly, which is what production code does.
//
// Rejection class: composability — producer returns int, consumer
// takes double.  V1 forbids implicit conversions across the fusion
// boundary (the "intermediate-in-registers" promise).
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/Fusion.h>

inline int    producer(int x)    noexcept { return x * 2; }
inline int    consumer(double x) noexcept { return static_cast<int>(x); }

int main() {
    // fuse<>() is constrained on `requires IsFusable<Fn1, Fn2>`.
    // The (int, double) composability check fails — substitution
    // failure on the function-template entry point.
    auto fused = ::crucible::safety::fuse<&producer, &consumer>();
    (void)fused;
    return 0;
}
