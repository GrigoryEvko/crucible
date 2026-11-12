// gcd takes two integers: the gcd of two floats does not compile.

#include <foundation/core/Arithmetic.h>

int main() { return ::foundation::core::gcd(4.0, 6.0) == 2u ? 0 : 1; }
