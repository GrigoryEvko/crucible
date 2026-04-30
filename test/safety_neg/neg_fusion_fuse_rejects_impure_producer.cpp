// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F07 fixture — pins fuse<>() rejection on impure producer.
// Fusion REORDERS effects in observable time — a non-pure callee
// makes that reordering observable, so V1 conservatively bans any
// producer or consumer with a non-empty inferred Met(X) effect row
// (cap-tag in the parameter list).  The producer here takes
// `effects::Alloc`, which is a cap-tag; the inferred row is non-empty,
// `is_pure_function_v` returns false, fusion rejects.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/Fusion.h>
#include <crucible/effects/Capabilities.h>

inline int producer(::crucible::effects::Alloc, int x) noexcept { return x; }
inline int consumer(int x) noexcept { return x + 1; }

int main() {
    auto fused = ::crucible::safety::fuse<&producer, &consumer>();
    (void)fused;
    return 0;
}
