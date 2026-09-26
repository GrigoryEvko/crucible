// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// sin_poly accepts an angle within one turn of zero.  The argument here is
// the first float above 2*pi, so the precondition of sin_poly refuses it.
// sin_poly is constexpr, so a constant evaluation reaches the check and the
// refusal is a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression, and
// the expansion note names the CRUCIBLE_PRE of sin_poly.

#include <fixy/fp/Polynomial.h>

static_assert(fixy::fp::sin_poly(0x1.921FB8p+2f) >= -1.0f);

int main() { return 0; }
