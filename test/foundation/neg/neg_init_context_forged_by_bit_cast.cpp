// An initialization context, built from a byte. A forged Init context
// admits any scope to the gates of process startup. Each constructor is
// user-provided, so the type is not trivially copyable, and
// std::bit_cast has no candidate.

#include <foundation/effects/Effect.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    auto forged = std::bit_cast<fe::Init>(char{0});
    (void)forged;
    return 0;
}
