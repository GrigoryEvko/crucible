// An initialization context, built over a buffer. A forged Init context
// admits any scope to the gates of process startup. No constructor is
// trivial, so the type is not an implicit-lifetime type, and the mandate
// of std::start_lifetime_as refuses it.

#include <foundation/effects/Effect.h>

#include <bit>
#include <memory>

namespace fe = ::foundation::effects;

int main() {
    alignas(8) unsigned char storage[8]{};
    auto* forged = std::start_lifetime_as<fe::Init>(storage);
    (void)forged;
    return 0;
}
