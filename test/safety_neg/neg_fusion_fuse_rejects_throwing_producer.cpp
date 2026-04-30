// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F07 fixture — pins fuse<>() rejection on noexcept(false)
// producer.  Throwing under fusion changes observable behaviour:
// an exception thrown by Fn1 in the un-fused form is observed AFTER
// its full output is computed; under fusion the partial-output is
// in register state and cannot be observed cleanly.  V1 conservatively
// bans both throwing producer and throwing consumer at the predicate
// level — verified here through the fuse<>() entry point.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/Fusion.h>

inline int producer(int x)          { return x * 2; }   // not noexcept
inline int consumer(int x) noexcept { return x + 1; }

int main() {
    auto fused = ::crucible::safety::fuse<&producer, &consumer>();
    (void)fused;
    return 0;
}
