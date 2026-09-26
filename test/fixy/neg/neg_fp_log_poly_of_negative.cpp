// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// log_poly accepts a positive normal float.  The reduction masks the sign
// bit away, so without the precondition log_poly(-2) would return log(2).
// log_poly is constexpr, so a constant evaluation reaches the check and the
// refusal is a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression, and
// the expansion note names the CRUCIBLE_PRE of log_poly.

#include <fixy/fp/Polynomial.h>

static_assert(fixy::fp::log_poly(-2.0f) >= 0.0f);

int main() { return 0; }
