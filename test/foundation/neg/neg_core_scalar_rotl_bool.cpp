// rotl takes an UnsignedInteger, and the Integer gate refuses bool.  A bool
// has one value bit, so a rotation of it has no meaning.

#include <foundation/core/Bits.h>

int main() { return ::foundation::core::rotl(true, 1) ? 0 : 1; }
