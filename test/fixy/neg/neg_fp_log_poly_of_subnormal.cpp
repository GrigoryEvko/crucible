// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// log_poly accepts a positive normal float.  A subnormal is above zero, so
// a check of x > 0 alone would admit it, but its exponent field is zero and
// its significand has no implicit leading one.  Without the precondition
// the reduction would read it as a normal number near 2^-127.  log_poly is
// constexpr, so a constant evaluation reaches the check and the refusal is
// a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression, and
// the expansion note names the CRUCIBLE_PRE of log_poly.

#include <fixy/fp/Polynomial.h>

#include <limits>

static_assert(fixy::fp::log_poly(std::numeric_limits<float>::denorm_min()) <= 0.0f);

int main() { return 0; }
