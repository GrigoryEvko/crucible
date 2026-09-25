// NEGATIVE-COMPILE TEST.  This file must not compile.
//
// no_overflow_mul_oracle<T> requires std::integral T.  It widens the two
// operands to a larger integer type and compares the product with the
// limits of T, and these limits have a meaning only for an integer type.
// A call with two double operands deduces T = double.  double does not
// satisfy std::integral, so the call has no viable candidate.

#include <foundation/contracts/DecideOracle.h>

int main() { return ::foundation::decide::oracle::no_overflow_mul_oracle(1.0, 2.0) ? 0 : 1; }
