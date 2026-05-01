// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT (#852): Bounded<Lo, Hi, T> with Lo > Hi — empty range.
//
// Violation: Bounded<10, 5, int> describes the empty interval [10, 5]
// — no integer satisfies it, the refinement is uninhabited.  Caught
// at the type level by the `bounded_alias` trampoline's
// `static_assert(Lo <= Hi)`, firing at alias instantiation rather
// than failing the contract for every constructed value.
//
// Expected diagnostic: `static_assert(Lo <= Hi)` inside
// detail::bounded_alias fires when (10, 5) is substituted.

#include <crucible/safety/RefinedAlgebra.h>

using crucible::safety::Bounded;

// Naming the type instantiates the bounded_alias trampoline,
// firing the static_assert.
using BadBounded = Bounded<10, 5, int>;

BadBounded g_bad{0, BadBounded::Trusted{}};
