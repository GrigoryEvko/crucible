// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The nearest multiple of pi/2 for -1e10 is about -6.4e9, which int32
// cannot hold, so the conversion in the reduction would be undefined
// behaviour.  The precondition of reduce_quarter_pi refuses the angle
// first.  reduce_quarter_pi is constexpr, so a constant evaluation reaches
// the check and the refusal is a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression, and
// the expansion note names the CRUCIBLE_PRE of reduce_quarter_pi.

#include <fixy/fp/Polynomial.h>

static_assert(fixy::fp::reduce_quarter_pi(-1.0e10f).quadrant >= 0);

int main() { return 0; }
