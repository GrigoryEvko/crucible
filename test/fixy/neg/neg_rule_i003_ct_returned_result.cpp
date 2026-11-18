// I003: constant time x a failure path, through a returned Result.
//
// Result<T, E> is the spelling of a failure that can occur.  A
// constant-time body that returns one gives its caller a discriminant to
// branch on, and the condition that chose the discriminant reads
// classified data.  The error type is plain, so I002 refuses the same
// pack: the error value leaves the classified binding.

#include <fixy/Core.h>
#include <fixy/Fn.h>

#include <cstdint>

enum class fault : std::uint8_t {
    refused = 1,
};

using Refused = ::fixy::fn<::fixy::Result<int, fault>, ::fixy::atom::constant_time>;

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
