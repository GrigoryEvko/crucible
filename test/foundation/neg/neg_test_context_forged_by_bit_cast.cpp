// A test context, built from a byte. A forged Test context admits
// production code to the gates of the test driver. Each constructor is
// user-provided, so the type is not trivially copyable, and
// std::bit_cast has no candidate.

#include <foundation/effects/Effect.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    auto forged = std::bit_cast<fe::Test>(char{0});
    (void)forged;
    return 0;
}
