// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// log_poly accepts a positive normal float.  Zero has an exponent field of
// zero, so without the precondition the reduction would read it as 2^-127
// and return a finite value for log(0).  log_poly is constexpr, so a
// constant evaluation reaches the check and the refusal is a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression, and
// the expansion note names the CRUCIBLE_PRE of log_poly.

#include <fixy/fp/Polynomial.h>

static_assert(fixy::fp::log_poly(0.0f) <= 0.0f);

int main() { return 0; }
