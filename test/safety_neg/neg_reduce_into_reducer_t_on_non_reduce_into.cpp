// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D07-AUDIT — sibling-pattern parity with
// neg_is_owned_region_tag_t_on_non_region.cpp.
//
// Violation: instantiating reduce_into_reducer_t<T> where T is not a
// reduce_into specialization.  The alias is constrained on
// is_reduce_into_v<T>, so non-reduce_into arguments are rejected at
// the requires clause rather than producing `void` (which would be a
// silent failure).
//
// Concrete bug-class this catches: without the constraint, a stray
// `reduce_into_reducer_t<MyHelperStruct>` in a fold-template body
// would silently resolve to `void`, which would then be invoked as
// `void(R const&, R const&)` downstream — a more confusing diagnostic
// at the call site, far from the type misuse.  With the constraint,
// the alias rejects non-reduce_into at the boundary where it's first
// instantiated.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsReduceInto.h>

struct NotReduceInto {
    int    accumulator;
    int   (*reducer)(int, int);
};

int main() {
    // NotReduceInto has the same field-by-name shape as reduce_into
    // but is not a specialization → alias is ill-formed.
    using R =
        crucible::safety::extract::reduce_into_reducer_t<NotReduceInto>;
    R const r{};
    (void)r;
    return 0;
}
