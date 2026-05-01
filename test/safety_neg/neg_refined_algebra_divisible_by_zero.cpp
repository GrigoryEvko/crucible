// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT (#852): DivisibleBy<0> static_assert witness.
//
// Violation: instantiating DivisibleBy<0> would compute `x % 0`
// at runtime — undefined behavior.  Caught at the type level so
// `divisible_by<0>` fires at instantiation rather than producing
// runtime UB.
//
// Expected diagnostic: `static_assert(Divisor != 0)` inside
// safety::DivisibleBy fires when 0 is substituted.

#include <crucible/safety/RefinedAlgebra.h>

using crucible::safety::Refined;
using crucible::safety::divisible_by;

// Naming the type instantiates DivisibleBy<0>, firing the
// static_assert.
using BadDiv = Refined<divisible_by<0>, int>;

BadDiv g_bad{0, BadDiv::Trusted{}};
