// I003: constant time x a failure path, through a returned Option.
//
// An Option has no error value, but its discriminant still tells the
// caller whether the body found a value.  A constant-time body that
// returns one gives its caller a branch on a condition that reads
// classified data.  I002 stands down, because no error value leaves the
// binding.

#include <fixy/Core.h>
#include <fixy/Fn.h>

using Refused = ::fixy::fn<::fixy::Option<int>, ::fixy::atom::constant_time>;

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
