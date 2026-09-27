// The sum of one argument is that argument only when it is a concurrent
// row.  A type that is not a row names no sum.

#include <foundation/effects/Concurrent.h>

namespace fe = ::foundation::effects;

static_assert(requires { typename fe::concurrent_row_n_t<int>; });

int main() { return 0; }
