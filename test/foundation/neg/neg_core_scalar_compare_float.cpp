// compare gives the exact order of two integers.  Two floats have no
// strong order, so compare of a double does not compile.

#include <foundation/core/Compare.h>

int main() { return ::foundation::core::compare(1.0, 2.0) < 0 ? 0 : 1; }
