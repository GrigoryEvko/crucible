// A character is not a number: the checked sum of two characters does not
// compile.

#include <foundation/core/Arithmetic.h>

int main() { return ::foundation::core::checked_add('a', 'b').is_some() ? 0 : 1; }
