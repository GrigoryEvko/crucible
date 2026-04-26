// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: copy-constructing a Linear<T>.  Linear<T> is the
// foundational move-only-resource wrapper for the L0 ownership axiom
// — duplicating a Linear<T> would create two simultaneous owners and
// allow double-consume.  The copy constructor is `= delete` with the
// named reason "linear — duplicating creates two simultaneous owners".
//
// The diagnostic carries the substring "move-only" (per Linear.h's
// = delete reason text), which the harness pattern-matches.  This
// pins the discipline at the foundational L0 wrapper that
// `Linear<T> = Graded<Absolute, QttSemiring::At<One>, T>` (per
// MIGRATE-1 #461) WILL alias once the migration lands.  If a future
// edit accidentally relaxes the copy constraint (e.g. by removing
// `= delete` or by adding an `operator T()` conversion), this test
// fires before the relaxation reaches downstream callers.
//
// Companion to neg_assignment.cpp (which exercises the assignment
// branch).  Together they cover the move-only contract from both
// directions.
//
// Task #146 (A8-P2 Neg-compile coverage for non-ScopedView primitives);
// see include/crucible/safety/Linear.h.

#include <crucible/safety/Linear.h>

int main() {
    using crucible::safety::Linear;
    Linear<int> a{42};

    // Should FAIL: copy constructor deleted with reason
    //   "Linear<T> is move-only; use std::move or drop()".
    Linear<int> b = a;

    return 0;
}
