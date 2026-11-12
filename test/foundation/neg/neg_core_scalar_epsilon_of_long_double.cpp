// long double has 80 value bits on x86_64 and 128 on aarch64, so it is
// not a Float, and its constants do not compile.

#include <foundation/core/Number.h>

int main() { return ::foundation::core::epsilon_of<long double> > 0 ? 0 : 1; }
