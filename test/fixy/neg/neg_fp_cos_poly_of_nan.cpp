// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A NaN is outside one turn, because every comparison with a NaN is false.
// Without the precondition of cos_poly the reduction would convert the NaN
// to int32, which is undefined behaviour.  cos_poly is constexpr, so a
// constant evaluation reaches the check and the refusal is a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression, and
// the expansion note names the CRUCIBLE_PRE of cos_poly.

#include <fixy/fp/Polynomial.h>

#include <limits>

static_assert(fixy::fp::cos_poly(std::numeric_limits<float>::quiet_NaN()) >= -1.0f);

int main() { return 0; }
