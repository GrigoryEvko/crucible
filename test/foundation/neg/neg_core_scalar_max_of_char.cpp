// Plain char is signed on x86_64 and unsigned on aarch64, so its limit
// changes with the target.  A limit of a character type does not compile.

#include <foundation/core/Number.h>

int main() { return ::foundation::core::max_of<char> > 0 ? 0 : 1; }
