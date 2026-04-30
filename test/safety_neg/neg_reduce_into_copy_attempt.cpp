// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D07-AUDIT — move-only fence on the reduce_into wrapper.
//
// Violation: copy-constructing a reduce_into<R, Op>.  reduce_into is
// move-only because the accumulator R is unique state.  Copying would
// duplicate the accumulator, breaking the linearity that makes
// parallel-reduce safe — two parallel branches each combining into
// the same accumulator-shape would produce results that depend on
// schedule, defeating DetSafe.
//
// The copy constructor is `= delete` (the in-class declaration uses
// the unattributed form to keep the surface minimal); the
// move-only invariant is statically asserted by
// reduce_into.h's self-test block:
//
//   static_assert(!std::is_copy_constructible_v<reduce_into<int, PlusOp>>);
//   static_assert(!std::is_copy_assignable_v<reduce_into<int, PlusOp>>);
//
// This file pins the same invariant from the consumer side: if a
// future edit accidentally relaxes the copy constraint (for instance
// by removing the `= delete` line), the test_migration_verification
// suite would not catch the regression because that suite checks
// composability, not copy-construction.  This neg-compile fixture
// catches it directly.
//
// [GCC-WRAPPER-TEXT] — overload-resolution diagnostic naming the
// deleted copy constructor.

#include <crucible/safety/reduce_into.h>

struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a + b;
    }
};

int main() {
    using crucible::safety::reduce_into;

    reduce_into<int, PlusOp> r1{0, PlusOp{}};

    // Should FAIL: reduce_into<R, Op> deletes its copy constructor.
    // The accumulator is unique state (linear semantics) — duplicating
    // would break parallel-reduce linearity.  Use std::move(r1) to
    // transfer ownership.
    reduce_into<int, PlusOp> r2 = r1;
    (void)r2;

    return 0;
}
