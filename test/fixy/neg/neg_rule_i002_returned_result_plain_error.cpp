// I002: classified x a failure whose error type is not Secret, through a
// returned Result.
//
// The binding states no Security atom, so it takes the strict pole and is
// classified.  Its payload is a Result whose error type is plain, so the
// error value leaves the classified binding and carries what the body
// knew when it failed.

#include <fixy/Core.h>
#include <fixy/Fn.h>

#include <cstdint>

enum class fault : std::uint8_t {
    refused = 1,
};

using Refused = ::fixy::fn<::fixy::Result<int, fault>>;

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
