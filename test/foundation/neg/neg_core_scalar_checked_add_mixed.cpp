// The two operands of an operation have one type, so no promotion hides a
// conversion: the sum of an int and an unsigned int does not compile.

#include <foundation/core/Arithmetic.h>

int main() { return ::foundation::core::checked_add(1, 2u).is_some() ? 0 : 1; }
