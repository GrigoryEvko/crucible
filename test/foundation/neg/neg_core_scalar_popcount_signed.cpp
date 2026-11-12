// popcount takes an unsigned value.  The sign bit of a signed value has no
// meaning in a count, so a call with a signed value does not compile.

#include <foundation/core/Bits.h>

int main() { return ::foundation::core::popcount(-1) == 32 ? 0 : 1; }
