// widening_mul takes two unsigned values: the product of two signed values
// does not compile.

#include <foundation/core/Arithmetic.h>

int main() { return ::foundation::core::widening_mul(-2, 3).low == 0 ? 0 : 1; }
