// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D07-AUDIT — sibling-pattern parity with
// neg_is_owned_region_value_t_on_non_region.cpp.
//
// Violation: instantiating reduce_into_accumulator_t<T> where T is not
// a reduce_into specialization.  The alias is constrained on
// is_reduce_into_v<T>, so non-reduce_into arguments are rejected at
// the requires clause rather than producing `void` (which would be a
// silent failure).
//
// Concrete bug-class this catches: a refactor that drops the
// `requires is_reduce_into_v<T>` constraint on
// reduce_into_accumulator_t would let
// `reduce_into_accumulator_t<int>` resolve to `void` from the primary
// template's `accumulator_type = void;`, propagating that void
// silently into downstream type machinery (e.g. the FOUND-D14
// Reduction concept's accumulator-shape check, which expects an int).
// With the constraint, the alias rejects non-reduce_into at the call
// boundary.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsReduceInto.h>

int main() {
    // int is not a reduce_into → alias is ill-formed.
    using A =
        crucible::safety::extract::reduce_into_accumulator_t<int>;
    A const a{};
    (void)a;
    return 0;
}
