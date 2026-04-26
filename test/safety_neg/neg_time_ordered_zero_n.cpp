// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: instantiating TimeOrdered<T, N=0>.
//
// The wrapper carries an explicit static_assert(N > 0, ...) mirroring
// the underlying HappensBeforeLattice's primary-template guard.
// Without this, a degenerate TimeOrdered<T, 0> would compile down to
// a Graded over an empty vector clock — algebraically vacuous, every
// pair "concurrent" but trivially equal.  Both static_asserts (the
// wrapper's and the lattice's) fire on instantiation, but the
// wrapper-level assert produces a more direct diagnostic.
//
// Pins the wrapper-level N>0 contract: a future refactor that
// removed the wrapper static_assert (delegating entirely to the
// lattice) would still be SOUND but would surface a less direct
// diagnostic; this neg-compile pins the named wrapper-level guard.
//
// [FRAMEWORK-CONTROLLED] — diagnostic regex matches the wrapper's
// static_assert string.

#include <crucible/safety/TimeOrdered.h>

int main() {
    // Should FAIL: N=0 trips TimeOrdered's static_assert.
    using TO0 = ::crucible::safety::TimeOrdered<int, 0>;
    TO0 evt{};
    return evt.peek();
}
